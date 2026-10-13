#include <wormedit/core/rational.h>
#include "../core/checked_math.h"
#include <wormedit/core/timing.h>

#include "timing_arithmetic.h"
#include "wide_math.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <ranges>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace wormedit {
namespace {

constexpr auto maximum_i64_magnitude =
    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
constexpr auto minimum_i64_magnitude = maximum_i64_magnitude + 1;

auto error(timing_error_code_t code, std::string message,
           std::optional<object_id_t> object = std::nullopt) -> timing_diagnostic_t;
auto timing_fingerprint(const chart_document_t& chart) -> std::uint64_t;
void fingerprint_mix(std::uint64_t& fingerprint, std::uint64_t value);
void fingerprint_position(std::uint64_t& fingerprint, local_position_t position);

using checked_math::magnitude;

using checked_math::checked_multiply;

using timing_arithmetic::try_normalize;

using timing_arithmetic::add_exact;

using timing_arithmetic::subtract_exact;

auto compare_positive_fractions(std::uint64_t left_numerator, std::uint64_t left_denominator,
                                std::uint64_t right_numerator, std::uint64_t right_denominator)
    -> int {
    auto reversed = false;
    while (true) {
        const auto left_whole = left_numerator / left_denominator;
        const auto right_whole = right_numerator / right_denominator;
        if (left_whole != right_whole) {
            const auto comparison = left_whole < right_whole ? -1 : 1;
            return reversed ? -comparison : comparison;
        }
        const auto left_remainder = left_numerator % left_denominator;
        const auto right_remainder = right_numerator % right_denominator;
        if (left_remainder == 0 || right_remainder == 0) {
            if (left_remainder == right_remainder) {
                return 0;
            }
            const auto comparison = left_remainder == 0 ? -1 : 1;
            return reversed ? -comparison : comparison;
        }
        left_numerator = left_denominator;
        left_denominator = left_remainder;
        right_numerator = right_denominator;
        right_denominator = right_remainder;
        reversed = !reversed;
    }
}

auto compare_exact(signed_rational_t left, signed_rational_t right) -> std::optional<int> {
    const auto left_normal = try_normalize(left);
    const auto right_normal = try_normalize(right);
    if (!left_normal || !right_normal) {
        return std::nullopt;
    }
    const bool left_negative = left_normal->numerator < 0;
    const bool right_negative = right_normal->numerator < 0;
    if (left_negative != right_negative) {
        return left_negative ? -1 : 1;
    }
    auto comparison =
        compare_positive_fractions(magnitude(left_normal->numerator), left_normal->denominator,
                                   magnitude(right_normal->numerator), right_normal->denominator);
    if (left_negative) {
        comparison = -comparison;
    }
    return comparison;
}

auto positive_fraction(std::vector<std::uint64_t> numerators,
                       std::vector<std::uint64_t> denominators)
    -> std::optional<signed_rational_t> {
    if (std::ranges::find(denominators, std::uint64_t{}) != denominators.end()) {
        return std::nullopt;
    }
    if (std::ranges::find(numerators, std::uint64_t{}) != numerators.end()) {
        return signed_rational_t{};
    }
    for (auto& numerator : numerators) {
        for (auto& denominator : denominators) {
            const auto divisor = std::gcd(numerator, denominator);
            numerator /= divisor;
            denominator /= divisor;
        }
    }
    auto numerator = std::uint64_t{1};
    auto denominator = std::uint64_t{1};
    for (const auto factor : numerators) {
        const auto product = checked_multiply(numerator, factor);
        if (!product || *product > maximum_i64_magnitude) {
            return std::nullopt;
        }
        numerator = *product;
    }
    for (const auto factor : denominators) {
        const auto product = checked_multiply(denominator, factor);
        if (!product) {
            return std::nullopt;
        }
        denominator = *product;
    }
    return try_normalize({static_cast<std::int64_t>(numerator), denominator});
}

auto signed_from_magnitude(std::uint64_t value, bool negative) -> std::optional<signed_rational_t> {
    const auto limit = negative ? minimum_i64_magnitude : maximum_i64_magnitude;
    if (value > limit) {
        return std::nullopt;
    }
    if (negative && value == minimum_i64_magnitude) {
        return signed_rational_t{std::numeric_limits<std::int64_t>::min(), 1};
    }
    const auto signed_value = static_cast<std::int64_t>(value);
    return signed_rational_t{negative ? -signed_value : signed_value, 1};
}

auto multiply_exact(signed_rational_t left, signed_rational_t right)
    -> std::optional<signed_rational_t> {
    const auto left_normal = try_normalize(left);
    const auto right_normal = try_normalize(right);
    if (!left_normal || !right_normal) {
        return std::nullopt;
    }
    if (left_normal->numerator == 0 || right_normal->numerator == 0) {
        return signed_rational_t{};
    }

    auto left_numerator = magnitude(left_normal->numerator);
    auto right_numerator = magnitude(right_normal->numerator);
    auto left_denominator = left_normal->denominator;
    auto right_denominator = right_normal->denominator;
    auto divisor = std::gcd(left_numerator, right_denominator);
    left_numerator /= divisor;
    right_denominator /= divisor;
    divisor = std::gcd(right_numerator, left_denominator);
    right_numerator /= divisor;
    left_denominator /= divisor;

    const auto numerator = checked_multiply(left_numerator, right_numerator);
    const auto denominator = checked_multiply(left_denominator, right_denominator);
    const bool negative = (left_normal->numerator < 0) != (right_normal->numerator < 0);
    if (!numerator || !denominator ||
        *numerator > (negative ? minimum_i64_magnitude : maximum_i64_magnitude)) {
        return std::nullopt;
    }
    auto result = signed_from_magnitude(*numerator, negative);
    if (!result) {
        return std::nullopt;
    }
    result->denominator = *denominator;
    return try_normalize(*result);
}

auto rational_progress(signed_rational_t elapsed, rational_t duration)
    -> std::optional<signed_rational_t> {
    const auto value = try_normalize(elapsed);
    if (!value || value->numerator < 0 || duration.denominator == 0) {
        return std::nullopt;
    }
    if (duration.numerator == 0) {
        return signed_rational_t{1, 1};
    }
    const auto comparison = compare_positive_fractions(
        magnitude(value->numerator), value->denominator, duration.numerator, duration.denominator);
    if (comparison >= 0) {
        return signed_rational_t{1, 1};
    }
    return positive_fraction({magnitude(value->numerator), duration.denominator},
                             {value->denominator, duration.numerator});
}

auto interpolate_exact(signed_rational_t begin, signed_rational_t end, signed_rational_t progress)
    -> std::optional<signed_rational_t> {
    const auto difference = subtract_exact(end, begin);
    const auto scaled = difference ? multiply_exact(*difference, progress) : std::nullopt;
    return scaled ? add_exact(begin, *scaled) : std::nullopt;
}

auto compare_row_to_integer(signed_rational_t row, std::uint64_t integer) -> std::optional<int> {
    const auto value = try_normalize(row);
    if (!value) {
        return std::nullopt;
    }
    if (value->numerator < 0) {
        return -1;
    }
    return compare_positive_fractions(magnitude(value->numerator), value->denominator, integer, 1);
}

auto seconds_inside_exact_segment(const timing_segment_t& segment, signed_rational_t row)
    -> timing_query_result_t {
    if (segment.warped) {
        return {.seconds = segment.begin_seconds, .inside_warp = true};
    }
    const auto offset = subtract_exact(row, {static_cast<std::int64_t>(segment.begin_row), 1});
    const auto width = segment.end_row - segment.begin_row;
    if (!offset || offset->numerator < 0 || width == 0) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "scroll/speed row interpolation exceeds exact bounds")};
    }
    const auto progress =
        positive_fraction({magnitude(offset->numerator)}, {offset->denominator, width});
    const auto duration = subtract_exact(segment.end_seconds, segment.begin_seconds);
    const auto delta = duration && progress ? multiply_exact(*duration, *progress) : std::nullopt;
    const auto seconds = delta ? add_exact(segment.begin_seconds, *delta) : std::nullopt;
    if (!seconds) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "scroll/speed time interpolation exceeds exact bounds")};
    }
    return {.seconds = *seconds};
}

auto seconds_inside_bounded_segment(const timing_segment_t& segment, signed_rational_t row)
    -> timing_query_result_t {
    if (segment.warped) {
        return {.seconds = segment.begin_seconds, .inside_warp = true};
    }
    const auto offset = subtract_exact(row, {static_cast<std::int64_t>(segment.begin_row), 1});
    const auto width = segment.end_row - segment.begin_row;
    const auto begin = timing_arithmetic::clock_ticks(segment.begin_seconds);
    const auto end = timing_arithmetic::clock_ticks(segment.end_seconds);
    if (!offset || offset->numerator < 0 || width == 0 || !begin || !end) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "bounded timing row interpolation exceeds its range")};
    }
    const auto seconds = timing_arithmetic::interpolate_clock(*begin, *end, *offset, width);
    if (!seconds) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "bounded timing interpolation exceeds its range")};
    }
    return {.seconds = *seconds};
}

auto seconds_inside_segment(const timing_index_t& timing, const timing_segment_t& segment,
                            signed_rational_t row) -> timing_query_result_t {
    return timing.bounded_clock ? seconds_inside_bounded_segment(segment, row)
                                : seconds_inside_exact_segment(segment, row);
}

auto timing_seconds_at_row(const timing_index_t& timing, signed_rational_t absolute_row,
                           timing_boundary_side_t side) -> timing_query_result_t {
    const auto row = try_normalize(absolute_row);
    if (!row) {
        return {.error = error(timing_error_code_t::invalid_document,
                               "scroll/speed query requires a valid rational row")};
    }
    if (row->numerator < 0 || timing.rows.empty() || timing.divisions_per_standard_measure == 0 ||
        timing.segments.size() + 1 != timing.rows.size() || timing.rows.front().absolute_row != 0 ||
        timing.rows.back().absolute_row != timing.total_rows) {
        return {.error = error(timing_error_code_t::position_out_of_range,
                               "scroll/speed query lies outside a complete timing index")};
    }
    const auto final_comparison = compare_row_to_integer(*row, timing.total_rows);
    if (!final_comparison || *final_comparison > 0) {
        return {.error = error(timing_error_code_t::position_out_of_range,
                               "scroll/speed query lies outside chart geometry")};
    }

    const auto boundary =
        std::lower_bound(timing.rows.begin(), timing.rows.end(), *row,
                         [](const timing_row_state_t& candidate, signed_rational_t query) {
                             const auto comparison =
                                 compare_row_to_integer(query, candidate.absolute_row);
                             return comparison && *comparison > 0;
                         });
    if (boundary != timing.rows.end()) {
        const auto comparison = compare_row_to_integer(*row, boundary->absolute_row);
        if (comparison && *comparison == 0) {
            const auto seconds =
                side == timing_boundary_side_t::before_delays ? boundary->before_delays_seconds
                : side == timing_boundary_side_t::after_stops ? boundary->after_stops_seconds
                                                              : boundary->event_seconds;
            return {.seconds = seconds, .inside_warp = boundary->inside_warp};
        }
    }

    const auto after_segment =
        std::upper_bound(timing.segments.begin(), timing.segments.end(), *row,
                         [](signed_rational_t query, const timing_segment_t& segment) {
                             const auto comparison =
                                 compare_row_to_integer(query, segment.begin_row);
                             return comparison && *comparison < 0;
                         });
    if (after_segment == timing.segments.begin()) {
        return {.error = error(timing_error_code_t::position_out_of_range,
                               "scroll/speed query has no containing timing segment")};
    }
    const auto& segment = *std::prev(after_segment);
    const auto end_comparison = compare_row_to_integer(*row, segment.end_row);
    if (!end_comparison || *end_comparison >= 0 || segment.begin_row > maximum_i64_magnitude) {
        return {.error = error(timing_error_code_t::position_out_of_range,
                               "scroll/speed query has no containing timing segment")};
    }
    return seconds_inside_segment(timing, segment, *row);
}

auto speed_segment_value(const speed_curve_segment_t& segment, signed_rational_t row,
                         signed_rational_t seconds, std::uint64_t divisions)
    -> std::optional<signed_rational_t> {
    if (segment.transition_duration.denominator == 0) {
        return std::nullopt;
    }
    if (segment.transition_duration.numerator == 0) {
        return try_normalize(segment.target_multiplier);
    }
    signed_rational_t elapsed;
    if (segment.transition_is_seconds) {
        const auto difference = subtract_exact(seconds, segment.begin_seconds);
        if (!difference || difference->numerator < 0) {
            return std::nullopt;
        }
        elapsed = *difference;
    } else {
        if (segment.begin_row > maximum_i64_magnitude || divisions == 0) {
            return std::nullopt;
        }
        const auto row_delta =
            subtract_exact(row, {static_cast<std::int64_t>(segment.begin_row), 1});
        if (!row_delta || row_delta->numerator < 0) {
            return std::nullopt;
        }
        const auto beats = positive_fraction({magnitude(row_delta->numerator), 4},
                                             {row_delta->denominator, divisions});
        if (!beats) {
            return std::nullopt;
        }
        elapsed = *beats;
    }
    const auto progress = rational_progress(elapsed, segment.transition_duration);
    return progress
               ? interpolate_exact(segment.initial_multiplier, segment.target_multiplier, *progress)
               : std::nullopt;
}

auto scroll_speed_fingerprint(const chart_document_t& chart) -> std::uint64_t {
    auto fingerprint = timing_fingerprint(chart);
    for (const auto& event : chart.scrolls) {
        fingerprint_mix(fingerprint, event.id.value);
        fingerprint_position(fingerprint, event.position);
        fingerprint_mix(fingerprint, magnitude(event.multiplier.numerator));
        fingerprint_mix(fingerprint, event.multiplier.numerator < 0);
        fingerprint_mix(fingerprint, event.multiplier.denominator);
        fingerprint_mix(fingerprint, event.provenance.source_order);
    }
    for (const auto& event : chart.speeds) {
        fingerprint_mix(fingerprint, event.id.value);
        fingerprint_position(fingerprint, event.position);
        fingerprint_mix(fingerprint, event.multiplier.numerator);
        fingerprint_mix(fingerprint, event.multiplier.denominator);
        fingerprint_mix(fingerprint, event.transition_beats.numerator);
        fingerprint_mix(fingerprint, event.transition_beats.denominator);
        fingerprint_mix(fingerprint, event.transition_is_seconds);
        fingerprint_mix(fingerprint, event.provenance.source_order);
    }
    return fingerprint;
}

auto row_duration(std::uint64_t rows, std::uint64_t resolution, signed_rational_t bpm)
    -> std::optional<signed_rational_t> {
    if (bpm.numerator <= 0 || bpm.denominator == 0 || resolution == 0) {
        return std::nullopt;
    }
    return positive_fraction({rows, 240, bpm.denominator},
                             {resolution, static_cast<std::uint64_t>(bpm.numerator)});
}

auto pause_duration(const pause_event_t& pause, signed_rational_t bpm)
    -> std::optional<signed_rational_t> {
    if (pause.duration.numerator < 0 || pause.duration.denominator == 0 || bpm.numerator <= 0 ||
        bpm.denominator == 0) {
        return std::nullopt;
    }
    const auto duration = static_cast<std::uint64_t>(pause.duration.numerator);
    switch (pause.source_unit) {
    case pause_unit_t::seconds:
        return try_normalize(pause.duration);
    case pause_unit_t::milliseconds:
        return positive_fraction({duration}, {pause.duration.denominator, 1'000});
    case pause_unit_t::beats:
        return positive_fraction(
            {duration, 60, bpm.denominator},
            {pause.duration.denominator, static_cast<std::uint64_t>(bpm.numerator)});
    case pause_unit_t::bms_stop_ticks:
        // One BMS STOP tick is 1/192 of a whole note, or 1/48 quarter beat.
        return positive_fraction(
            {duration, 5, bpm.denominator},
            {pause.duration.denominator, 4, static_cast<std::uint64_t>(bpm.numerator)});
    }
    return std::nullopt;
}

auto error(timing_error_code_t code, std::string message, std::optional<object_id_t> object)
    -> timing_diagnostic_t {
    return {.severity = diagnostic_severity_t::error,
            .code = code,
            .message = std::move(message),
            .object = object};
}

auto warning(timing_error_code_t code, std::string message,
             std::optional<object_id_t> object = std::nullopt) -> timing_diagnostic_t {
    return {.severity = diagnostic_severity_t::warning,
            .code = code,
            .message = std::move(message),
            .object = object};
}

auto has_errors(const std::vector<timing_diagnostic_t>& diagnostics) -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

struct positioned_bpm_t {
    std::uint64_t row{};
    const bpm_event_t* event{};
    std::size_t insertion_order{};
};

struct positioned_pause_t {
    std::uint64_t row{};
    const pause_event_t* event{};
    std::size_t insertion_order{};
};

auto order_key(const source_provenance_t& provenance, std::size_t insertion_order,
               object_id_t object) {
    return std::tuple{provenance.source_order, insertion_order, object.value};
}

auto position_frame(const timing_index_t& timing, std::optional<std::uint64_t> row,
                    const preview_schedule_options_t& options,
                    std::vector<timing_diagnostic_t>& diagnostics,
                    std::optional<object_id_t> object) -> std::optional<std::uint64_t> {
    if (!row) {
        diagnostics.push_back(error(timing_error_code_t::position_out_of_range,
                                    "preview event lies outside chart geometry", object));
        return std::nullopt;
    }
    const auto query = timing.seconds_at(*row, timing_boundary_side_t::event);
    if (!query) {
        diagnostics.push_back(*query.error);
        diagnostics.back().object = object;
        return std::nullopt;
    }
    if (query.inside_warp) {
        return std::nullopt;
    }
    const auto frame = sample_frame_at(*query.seconds, options.sample_rate, options.rounding);
    if (!frame) {
        auto diagnostic = *frame.error;
        diagnostic.object = object;
        if (diagnostic.code == timing_error_code_t::negative_sample_frame) {
            diagnostic.severity = diagnostic_severity_t::warning;
        }
        diagnostics.push_back(std::move(diagnostic));
        return std::nullopt;
    }
    return frame.frame;
}

auto boundary_frame(const timing_index_t& timing, std::optional<std::uint64_t> row,
                    timing_boundary_side_t side, const preview_schedule_options_t& options,
                    std::vector<timing_diagnostic_t>& diagnostics, object_id_t object)
    -> std::optional<std::uint64_t> {
    if (!row) {
        diagnostics.push_back(error(timing_error_code_t::position_out_of_range,
                                    "timing discontinuity lies outside chart geometry", object));
        return std::nullopt;
    }
    const auto query = timing.seconds_at(*row, side);
    if (!query) {
        diagnostics.push_back(*query.error);
        diagnostics.back().object = object;
        return std::nullopt;
    }
    const auto frame = sample_frame_at(*query.seconds, options.sample_rate, options.rounding);
    if (!frame) {
        auto diagnostic = *frame.error;
        diagnostic.object = object;
        if (diagnostic.code == timing_error_code_t::negative_sample_frame) {
            diagnostic.severity = diagnostic_severity_t::warning;
        }
        diagnostics.push_back(std::move(diagnostic));
        return std::nullopt;
    }
    return frame.frame;
}

auto visual_draw_order(visual_event_kind_t kind) -> std::size_t {
    switch (kind) {
    case visual_event_kind_t::background:
        return 0;
    case visual_event_kind_t::poor:
        return 1;
    case visual_event_kind_t::layer:
        return 2;
    case visual_event_kind_t::layer2:
        return 3;
    case visual_event_kind_t::video:
        return 4;
    case visual_event_kind_t::storyboard:
        return 5;
    }
    return 5;
}

void fingerprint_mix(std::uint64_t& fingerprint, std::uint64_t value) {
    // A deterministic non-cryptographic fingerprint used only as a stale-cache guard.
    fingerprint ^= value + 0x9e3779b97f4a7c15ULL + (fingerprint << 6U) + (fingerprint >> 2U);
}

void fingerprint_position(std::uint64_t& fingerprint, local_position_t position) {
    fingerprint_mix(fingerprint, position.measure.value);
    fingerprint_mix(fingerprint, position.row);
}

auto timing_fingerprint(const chart_document_t& chart) -> std::uint64_t {
    auto fingerprint = std::uint64_t{0x574f524d54494d45ULL};
    fingerprint_mix(fingerprint, static_cast<std::uint64_t>(chart.source_family));
    fingerprint_mix(fingerprint, chart.divisions_per_standard_measure);
    fingerprint_mix(fingerprint, magnitude(chart.beat_zero_audio_offset_seconds.numerator));
    fingerprint_mix(fingerprint, chart.beat_zero_audio_offset_seconds.numerator < 0);
    fingerprint_mix(fingerprint, chart.beat_zero_audio_offset_seconds.denominator);
    for (const auto& measure : chart.measures) {
        fingerprint_mix(fingerprint, measure.id.value);
        fingerprint_mix(fingerprint, measure.definition.length.numerator);
        fingerprint_mix(fingerprint, measure.definition.length.denominator);
        fingerprint_mix(fingerprint, measure.definition.meter.has_value());
        if (measure.definition.meter) {
            fingerprint_mix(fingerprint, measure.definition.meter->numerator);
            fingerprint_mix(fingerprint, measure.definition.meter->denominator);
        }
    }
    for (const auto& bpm : chart.bpms) {
        fingerprint_mix(fingerprint, bpm.id.value);
        fingerprint_position(fingerprint, bpm.position);
        fingerprint_mix(fingerprint, magnitude(bpm.beats_per_minute.numerator));
        fingerprint_mix(fingerprint, bpm.beats_per_minute.numerator < 0);
        fingerprint_mix(fingerprint, bpm.beats_per_minute.denominator);
        fingerprint_mix(fingerprint, bpm.provenance.source_order);
    }
    for (const auto& pause : chart.pauses) {
        fingerprint_mix(fingerprint, pause.id.value);
        fingerprint_position(fingerprint, pause.position);
        fingerprint_mix(fingerprint, static_cast<std::uint64_t>(pause.kind));
        fingerprint_mix(fingerprint, magnitude(pause.duration.numerator));
        fingerprint_mix(fingerprint, pause.duration.numerator < 0);
        fingerprint_mix(fingerprint, pause.duration.denominator);
        fingerprint_mix(fingerprint, static_cast<std::uint64_t>(pause.source_unit));
        fingerprint_mix(fingerprint, pause.provenance.source_order);
    }
    for (const auto& warp : chart.warps) {
        fingerprint_mix(fingerprint, warp.id.value);
        fingerprint_position(fingerprint, warp.span.begin);
        fingerprint_position(fingerprint, warp.span.end);
        fingerprint_mix(fingerprint, static_cast<std::uint64_t>(warp.source_encoding));
        fingerprint_mix(fingerprint, warp.provenance.source_order);
    }
    return fingerprint;
}

struct timing_build_context_t {
    const chart_document_t& chart;
    const measure_index_t& measures;
    const timing_build_options_t& options;
    std::vector<timing_diagnostic_t>& diagnostics;
    std::set<std::uint64_t> boundaries;
    std::vector<positioned_bpm_t> bpms;
    std::vector<positioned_pause_t> pauses;
    std::map<std::uint64_t, std::pair<std::size_t, std::size_t>> warp_edges;
};

auto insert_timing_boundary(timing_build_context_t& context, std::uint64_t row) -> bool {
    context.boundaries.insert(row);
    if (context.boundaries.size() <= context.options.maximum_boundaries) {
        return true;
    }
    context.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                        "timing boundary count exceeds the configured cap"));
    return false;
}

auto collect_timing_bpms(timing_build_context_t& context) -> bool {
    context.bpms.reserve(context.chart.bpms.size());
    for (std::size_t order = 0; order < context.chart.bpms.size(); ++order) {
        const auto& bpm = context.chart.bpms[order];
        const auto row = absolute_row(context.measures, bpm.position);
        if (!row) {
            context.diagnostics.push_back(error(timing_error_code_t::position_out_of_range,
                                                "BPM position is outside chart geometry", bpm.id));
            continue;
        }
        if (bpm.beats_per_minute.numerator <= 0 || bpm.beats_per_minute.denominator == 0) {
            context.diagnostics.push_back(
                error(timing_error_code_t::invalid_bpm, "BPM must be a positive rational", bpm.id));
            continue;
        }
        if (!insert_timing_boundary(context, *row)) {
            return false;
        }
        context.bpms.push_back({*row, &bpm, order});
    }
    std::ranges::stable_sort(context.bpms, [](const auto& left, const auto& right) {
        return std::tuple{left.row,
                          order_key(left.event->provenance, left.insertion_order, left.event->id)} <
               std::tuple{right.row, order_key(right.event->provenance, right.insertion_order,
                                               right.event->id)};
    });
    if (context.bpms.empty() || context.bpms.front().row != 0) {
        context.diagnostics.push_back(error(timing_error_code_t::missing_initial_bpm,
                                            "timing requires a positive BPM at chart row zero"));
    }
    return true;
}

auto collect_timing_pauses(timing_build_context_t& context) -> bool {
    context.pauses.reserve(context.chart.pauses.size());
    for (std::size_t order = 0; order < context.chart.pauses.size(); ++order) {
        const auto& pause = context.chart.pauses[order];
        const auto row = absolute_row(context.measures, pause.position);
        if (!row) {
            context.diagnostics.push_back(error(timing_error_code_t::position_out_of_range,
                                                "pause position is outside chart geometry",
                                                pause.id));
            continue;
        }
        if (pause.duration.numerator < 0 || pause.duration.denominator == 0) {
            context.diagnostics.push_back(error(timing_error_code_t::invalid_pause,
                                                "pause duration must be a nonnegative rational",
                                                pause.id));
            continue;
        }
        if (!insert_timing_boundary(context, *row)) {
            return false;
        }
        context.pauses.push_back({*row, &pause, order});
    }
    std::ranges::stable_sort(context.pauses, [](const auto& left, const auto& right) {
        return std::tuple{left.row,
                          order_key(left.event->provenance, left.insertion_order, left.event->id)} <
               std::tuple{right.row, order_key(right.event->provenance, right.insertion_order,
                                               right.event->id)};
    });
    return true;
}

auto collect_timing_warps(timing_build_context_t& context) -> bool {
    for (const auto& warp : context.chart.warps) {
        const auto begin = absolute_row(context.measures, warp.span.begin);
        const auto end = absolute_row(context.measures, warp.span.end);
        if (!begin || !end || *end <= *begin) {
            context.diagnostics.push_back(error(timing_error_code_t::invalid_warp,
                                                "warp must be a positive in-chart musical interval",
                                                warp.id));
            continue;
        }
        if (!insert_timing_boundary(context, *begin) || !insert_timing_boundary(context, *end)) {
            return false;
        }
        ++context.warp_edges[*begin].first;
        ++context.warp_edges[*end].second;
    }
    return true;
}

struct timing_compile_context_t {
    timing_build_context_t& source;
    timing_index_t& index;
    std::vector<std::uint64_t> boundaries;
    signed_rational_t current_time;
    signed_rational_t current_bpm{};
    std::size_t bpm_cursor{};
    std::size_t pause_cursor{};
    std::size_t active_warps{};
    bool bounded_clock{};
    std::size_t bounded_clock_roundings{};
    std::size_t maximum_bounded_clock_roundings{};
    bool accumulation_overflow{};
};

auto accumulate_timing_interval(timing_compile_context_t& context, signed_rational_t duration,
                                std::string message,
                                std::optional<object_id_t> object = std::nullopt) -> bool {
    if (!context.bounded_clock) {
        const auto added = add_exact(context.current_time, duration);
        if (added) {
            context.current_time = *added;
            return true;
        }
        context.accumulation_overflow = true;
    } else {
        if (context.bounded_clock_roundings >= context.maximum_bounded_clock_roundings) {
            context.source.diagnostics.push_back(error(timing_error_code_t::arithmetic_overflow,
                "bounded timing clock exceeds its configured rounding limit", object));
            return false;
        }
        const auto interval_ticks = timing_arithmetic::clock_ticks(duration);
        const auto current_ticks = timing_arithmetic::clock_ticks(context.current_time);
        if (!interval_ticks || *interval_ticks < 0 || !current_ticks ||
            (duration.numerator > 0 && *interval_ticks == 0)) {
            context.source.diagnostics.push_back(error(timing_error_code_t::arithmetic_overflow,
                "bounded timing interval is outside the clock's precision or range", object));
            return false;
        }
        const auto advanced = timing_arithmetic::advance_ticks(
            *current_ticks, static_cast<std::uint64_t>(*interval_ticks));
        if (!advanced) {
            context.source.diagnostics.push_back(error(timing_error_code_t::arithmetic_overflow,
                "bounded timing clock exceeds its signed tick range", object));
            return false;
        }
        context.current_time = timing_arithmetic::clock_value(*advanced);
        ++context.bounded_clock_roundings;
        return true;
    }
    context.source.diagnostics.push_back(error(timing_error_code_t::arithmetic_overflow,
                                               std::move(message), object));
    return false;
}

auto apply_timing_pause(timing_compile_context_t& context, const pause_event_t& pause,
                        const char* description) -> bool {
    const auto duration = pause_duration(pause, context.current_bpm);
    if (!duration) {
        context.source.diagnostics.push_back(
            error(timing_error_code_t::arithmetic_overflow,
                  std::string(description) + " conversion exceeds exact timing bounds", pause.id));
        return false;
    }
    if (context.active_warps != 0 && duration->numerator != 0) {
        context.source.diagnostics.push_back(warning(
            timing_error_code_t::invalid_warp,
            std::string(description) + " inside a warp is suppressed by the canonical timing map",
            pause.id));
        return true;
    }
    return accumulate_timing_interval(
        context, *duration,
        std::string(description) + " accumulation exceeds exact timing bounds", pause.id);
}

auto advance_timing_boundary(timing_compile_context_t& context, std::uint64_t row) -> bool {
    if (const auto edge = context.source.warp_edges.find(row);
        edge != context.source.warp_edges.end()) {
        if (edge->second.second > context.active_warps) {
            context.source.diagnostics.push_back(
                error(timing_error_code_t::invalid_warp, "warp edge accounting is inconsistent"));
            return false;
        }
        context.active_warps -= edge->second.second;
        context.active_warps += edge->second.first;
    }
    while (context.bpm_cursor < context.source.bpms.size() &&
           context.source.bpms[context.bpm_cursor].row == row) {
        context.current_bpm =
            *try_normalize(context.source.bpms[context.bpm_cursor].event->beats_per_minute);
        ++context.bpm_cursor;
    }
    if (context.current_bpm.numerator <= 0) {
        context.source.diagnostics.push_back(
            error(timing_error_code_t::missing_initial_bpm,
                  "no effective BPM exists at this timing boundary"));
        return false;
    }
    return true;
}

auto append_timing_row(timing_compile_context_t& context, std::uint64_t row) -> bool {
    if (!advance_timing_boundary(context, row)) {
        return false;
    }
    const auto row_pause_begin = context.pause_cursor;
    while (context.pause_cursor < context.source.pauses.size() &&
           context.source.pauses[context.pause_cursor].row == row) {
        ++context.pause_cursor;
    }
    const auto before_delays = context.current_time;
    for (auto index = row_pause_begin; index < context.pause_cursor; ++index) {
        if (context.source.pauses[index].event->kind == pause_kind_t::delay &&
            !apply_timing_pause(context, *context.source.pauses[index].event, "delay")) {
            return false;
        }
    }
    const auto event_time = context.current_time;
    for (auto index = row_pause_begin; index < context.pause_cursor; ++index) {
        if (context.source.pauses[index].event->kind == pause_kind_t::stop &&
            !apply_timing_pause(context, *context.source.pauses[index].event, "stop")) {
            return false;
        }
    }
    context.index.rows.push_back({.absolute_row = row,
                                  .before_delays_seconds = before_delays,
                                  .event_seconds = event_time,
                                  .after_stops_seconds = context.current_time,
                                  .effective_bpm = context.current_bpm,
                                  .inside_warp = context.active_warps != 0});
    return true;
}

auto append_timing_segment(timing_compile_context_t& context, std::uint64_t row,
                           std::uint64_t next_row) -> bool {
    auto segment_end = context.current_time;
    if (context.active_warps == 0) {
        const auto duration =
            row_duration(next_row - row, context.source.chart.divisions_per_standard_measure,
                         context.current_bpm);
        if (!duration) {
            context.source.diagnostics.push_back(
                error(timing_error_code_t::arithmetic_overflow,
                      "BPM segment exceeds exact timing arithmetic bounds"));
            return false;
        }
        const auto before_segment = context.current_time;
        if (!accumulate_timing_interval(
                context, *duration, "timing segment accumulation exceeds exact arithmetic bounds"))
            return false;
        segment_end = context.current_time;
        context.index.segments.push_back({.begin_row = row,
                                          .end_row = next_row,
                                          .begin_seconds = before_segment,
                                          .end_seconds = segment_end,
                                          .effective_bpm = context.current_bpm,
                                          .warped = false});
        return true;
    }
    context.index.segments.push_back({.begin_row = row,
                                      .end_row = next_row,
                                      .begin_seconds = context.current_time,
                                      .end_seconds = segment_end,
                                      .effective_bpm = context.current_bpm,
                                      .warped = context.active_warps != 0});
    context.current_time = segment_end;
    return true;
}

auto compile_timing_rows(timing_compile_context_t& context) -> bool {
    for (std::size_t boundary_index = 0; boundary_index < context.boundaries.size();
         ++boundary_index) {
        const auto row = context.boundaries[boundary_index];
        if (!append_timing_row(context, row)) {
            return false;
        }
        if (boundary_index + 1 == context.boundaries.size()) {
            continue;
        }
        if (!append_timing_segment(context, row, context.boundaries[boundary_index + 1])) {
            return false;
        }
    }
    return true;
}

struct timing_compile_attempt_t {
    std::optional<timing_index_t> index;
    bool accumulation_overflow{};
};

auto compile_timing_attempt(timing_build_context_t& source, signed_rational_t offset,
                            bool bounded_clock, std::size_t maximum_roundings)
    -> timing_compile_attempt_t {
    timing_index_t index{.divisions_per_standard_measure = source.chart.divisions_per_standard_measure,
                         .total_rows = source.measures.total_rows,
                         .source_fingerprint = timing_fingerprint(source.chart),
                         .discontinuity_profile = resolve_timing_discontinuity_profile(
                             source.chart.source_family, source.options.discontinuity_profile),
                         .bounded_clock = bounded_clock};
    const auto& boundary_set = source.boundaries;
    index.rows.reserve(boundary_set.size());
    index.segments.reserve(boundary_set.size() - 1);
    auto current_time = offset;
    auto roundings = std::size_t{};
    if (bounded_clock) {
        if (source.measures.total_rows > timing_arithmetic::positive_limit /
                                             timing_arithmetic::row_quantum) {
            source.diagnostics.push_back(error(timing_error_code_t::arithmetic_overflow,
                "chart rows exceed the bounded clock's nanorow range"));
            return {};
        }
        const auto ticks = timing_arithmetic::clock_ticks(offset);
        if (!ticks || maximum_roundings == 0) {
            source.diagnostics.push_back(error(timing_error_code_t::arithmetic_overflow,
                maximum_roundings == 0 ? "bounded timing clock rounding limit is zero"
                                       : "beat-zero offset exceeds the bounded clock range"));
            return {};
        }
        current_time = timing_arithmetic::clock_value(*ticks);
        roundings = 1;
    }
    auto compilation = timing_compile_context_t{
        source, index, std::vector<std::uint64_t>(boundary_set.begin(), boundary_set.end()),
        current_time, {}, 0, 0, 0, bounded_clock, roundings, maximum_roundings, false};
    if (!compile_timing_rows(compilation)) {
        return {.accumulation_overflow = compilation.accumulation_overflow};
    }
    index.bounded_clock_roundings = compilation.bounded_clock_roundings;
    return {.index = std::move(index)};
}

auto compile_timing_with_policy(timing_build_context_t& source, signed_rational_t offset,
                                bool allow_bounded_clock, std::size_t maximum_roundings)
    -> std::optional<timing_index_t> {
    const auto diagnostics_begin = source.diagnostics.size();
    auto exact = compile_timing_attempt(source, offset, false, maximum_roundings);
    if (exact.index) return std::move(exact.index);
    if (!allow_bounded_clock || !exact.accumulation_overflow) return std::nullopt;
    source.diagnostics.erase(source.diagnostics.begin() +
                                 static_cast<std::ptrdiff_t>(diagnostics_begin),
                             source.diagnostics.end());
    auto bounded = compile_timing_attempt(source, offset, true, maximum_roundings);
    if (!bounded.index) return std::nullopt;
    source.diagnostics.push_back(warning(
        timing_error_code_t::arithmetic_overflow,
        "Exact timing accumulation overflowed; using a bounded derived clock rounded to "
        "1e-12 seconds at " + std::to_string(bounded.index->bounded_clock_roundings) +
        " boundaries/intervals."));
    return std::move(bounded.index);
}

} // namespace

auto build_timing_index(const chart_document_t& chart, timing_build_options_t options)
    -> timing_build_result_t {
    timing_build_result_t result;
    if (options.discontinuity_profile &&
        !valid_timing_discontinuity_profile(*options.discontinuity_profile)) {
        result.diagnostics.push_back(error(timing_error_code_t::invalid_document,
                                           "timing discontinuity profile is not recognized"));
        return result;
    }
    const auto built_measures = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(built_measures)) {
        result.diagnostics.push_back(
            error(timing_error_code_t::invalid_document, "chart measure geometry is invalid"));
        return result;
    }
    const auto& measures = std::get<measure_index_t>(built_measures);
    if (chart.divisions_per_standard_measure == 0 || measures.total_rows == 0) {
        result.diagnostics.push_back(error(timing_error_code_t::invalid_document,
                                           "timing requires a nonempty positive-resolution chart"));
        return result;
    }
    if (options.maximum_boundaries == 0) {
        result.diagnostics.push_back(
            error(timing_error_code_t::capacity_exceeded, "timing boundary capacity is zero"));
        return result;
    }
    if (chart.bpms.size() > options.maximum_timing_events ||
        chart.pauses.size() > options.maximum_timing_events - chart.bpms.size() ||
        chart.warps.size() >
            options.maximum_timing_events - chart.bpms.size() - chart.pauses.size()) {
        result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                           "timing event count exceeds the configured cap"));
        return result;
    }
    const auto offset = try_normalize(chart.beat_zero_audio_offset_seconds);
    if (!offset) {
        result.diagnostics.push_back(error(timing_error_code_t::invalid_document,
                                           "beat-zero audio offset is not a valid rational"));
        return result;
    }

    timing_build_context_t work{
        chart, measures, options, result.diagnostics, {0, measures.total_rows}};
    if (work.boundaries.size() > options.maximum_boundaries) {
        result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                           "timing boundary count exceeds the configured cap"));
        return result;
    }
    if (!collect_timing_bpms(work) || !collect_timing_pauses(work) || !collect_timing_warps(work)) {
        return result;
    }
    if (has_errors(result.diagnostics)) {
        return result;
    }

    auto index = compile_timing_with_policy(work, *offset, options.allow_bounded_clock,
                                            std::min(options.maximum_bounded_clock_roundings,
                                                     std::size_t{300'001}));
    if (!index) {
        return result;
    }

    result.index = std::move(*index);
    return result;
}

namespace {

auto timing_at_exact_integer_row(const timing_index_t& index, std::uint64_t absolute_row,
                                timing_boundary_side_t side) -> timing_query_result_t {
    if (absolute_row > index.total_rows || index.rows.empty()) {
        return {.error = error(timing_error_code_t::position_out_of_range,
                               "timing query lies outside chart geometry")};
    }
    const auto exact =
        std::ranges::lower_bound(index.rows, absolute_row, {}, &timing_row_state_t::absolute_row);
    if (exact != index.rows.end() && exact->absolute_row == absolute_row) {
        const auto seconds =
            side == timing_boundary_side_t::before_delays ? exact->before_delays_seconds
            : side == timing_boundary_side_t::after_stops ? exact->after_stops_seconds
                                                          : exact->event_seconds;
        return {.seconds = seconds, .inside_warp = exact->inside_warp};
    }
    const auto segment =
        std::ranges::upper_bound(index.segments, absolute_row, {}, &timing_segment_t::begin_row);
    if (segment == index.segments.begin()) {
        return {.error = error(timing_error_code_t::position_out_of_range,
                               "timing query has no containing segment")};
    }
    const auto& containing = *std::prev(segment);
    if (absolute_row >= containing.end_row) {
        return {.error = error(timing_error_code_t::position_out_of_range,
                               "timing query has no containing segment")};
    }
    if (containing.warped) {
        return {.seconds = containing.begin_seconds, .inside_warp = true};
    }
    const auto duration =
        row_duration(absolute_row - containing.begin_row, index.divisions_per_standard_measure,
                     containing.effective_bpm);
    const auto seconds = duration ? add_exact(containing.begin_seconds, *duration) : std::nullopt;
    if (!seconds) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "timing query exceeds exact arithmetic bounds")};
    }
    return {.seconds = *seconds};
}

auto timing_at_bounded_integer_row(const timing_index_t& index, std::uint64_t absolute_row,
                                  timing_boundary_side_t side) -> timing_query_result_t {
    if (absolute_row > index.total_rows || index.rows.empty()) {
        return {.error = error(timing_error_code_t::position_out_of_range,
                               "timing query lies outside chart geometry")};
    }
    if (absolute_row > maximum_i64_magnitude) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "bounded timing row exceeds signed position bounds")};
    }
    return timing_seconds_at_row(index, {static_cast<std::int64_t>(absolute_row), 1}, side);
}

} // namespace

auto timing_at(const timing_index_t& index, std::uint64_t absolute_row,
               timing_boundary_side_t side) -> timing_query_result_t {
    return index.bounded_clock ? timing_at_bounded_integer_row(index, absolute_row, side)
                               : timing_at_exact_integer_row(index, absolute_row, side);
}

auto timing_at(const timing_index_t& index, exact_chart_row_t absolute_row,
               timing_boundary_side_t side) -> timing_query_result_t {
    if (absolute_row.fractional_rows.numerator == 0 &&
        absolute_row.fractional_rows.denominator != 0) {
        return index.seconds_at(absolute_row.whole_rows, side);
    }
    if (absolute_row.whole_rows > maximum_i64_magnitude ||
        absolute_row.fractional_rows.numerator > maximum_i64_magnitude ||
        absolute_row.fractional_rows.denominator == 0 ||
        absolute_row.fractional_rows.numerator >= absolute_row.fractional_rows.denominator) {
        return {.error = error(timing_error_code_t::position_out_of_range,
                               "fractional timing row lies outside chart geometry")};
    }
    const auto row = add_exact({static_cast<std::int64_t>(absolute_row.whole_rows), 1},
                               {static_cast<std::int64_t>(absolute_row.fractional_rows.numerator),
                                absolute_row.fractional_rows.denominator});
    if (!row) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "fractional timing row exceeds exact arithmetic bounds")};
    }
    return timing_seconds_at_row(index, *row, side);
}

auto sample_frame_at_chart_y(const timing_index_t& index, double viewport_scroll_pixels,
                             double pixels_per_standard_measure, double y_pixels,
                             double viewport_height_pixels, std::uint32_t sample_rate)
    -> std::optional<std::uint64_t> {
    if (!std::isfinite(y_pixels) || y_pixels < 0.0 || y_pixels > viewport_height_pixels ||
        !std::isfinite(viewport_height_pixels) || viewport_height_pixels <= 0.0 ||
        !std::isfinite(viewport_scroll_pixels) || !std::isfinite(pixels_per_standard_measure) ||
        pixels_per_standard_measure <= 0.0 || sample_rate == 0 ||
        index.divisions_per_standard_measure == 0) {
        return std::nullopt;
    }
    const auto row = (static_cast<long double>(y_pixels) + viewport_scroll_pixels) *
                     index.divisions_per_standard_measure / pixels_per_standard_measure;
    if (!std::isfinite(row) || row < 0.0L || row > index.total_rows) {
        return std::nullopt;
    }
    constexpr std::uint64_t fractional_scale = 1'000'000;
    auto whole = static_cast<std::uint64_t>(std::floor(row));
    auto fractional = static_cast<std::uint64_t>(
        std::llround((row - static_cast<long double>(whole)) * fractional_scale));
    if (fractional == fractional_scale) {
        ++whole;
        fractional = 0;
    }
    const auto at = index.seconds_at(exact_chart_row_t{whole, {fractional, fractional_scale}});
    const auto frame =
        at ? sample_frame_at(*at.seconds, sample_rate, sample_rounding_t::nearest_ties_to_even)
           : sample_frame_result_t{};
    return frame ? frame.frame : std::nullopt;
}

auto proposed_timing_y_on_fixed_waveform(const timing_index_t& original,
                                         const timing_index_t& proposed,
                                         exact_chart_row_t chart_row, double viewport_scroll_pixels,
                                         double pixels_per_standard_measure)
    -> std::optional<double> {
    if (!std::isfinite(viewport_scroll_pixels) || !std::isfinite(pixels_per_standard_measure) ||
        pixels_per_standard_measure <= 0.0 || original.divisions_per_standard_measure == 0 ||
        chart_row.fractional_rows.denominator == 0) {
        return std::nullopt;
    }
    const auto proposed_time = proposed.seconds_at(chart_row);
    if (!proposed_time)
        return std::nullopt;
    const auto original_time = original.seconds_at(chart_row);
    long double displayed_row{};
    if (original_time && original_time.seconds == proposed_time.seconds) {
        displayed_row =
            chart_row.whole_rows + to_long_double(chart_row.fractional_rows);
    } else {
        const auto position = original.row_at(*proposed_time.seconds,
                                                  {.extrapolate_before_chart = true});
        if (!position || position.absolute_row->denominator == 0)
            return std::nullopt;
        displayed_row = to_long_double(*position.absolute_row);
    }
    const auto y =
        displayed_row * pixels_per_standard_measure / original.divisions_per_standard_measure -
        viewport_scroll_pixels;
    if (!std::isfinite(y) || std::abs(y) > 10000000.0L)
        return std::nullopt;
    return static_cast<double>(y);
}

auto timing_at(const timing_index_t& index, const measure_index_t& measures,
               local_position_t position, timing_boundary_side_t side) -> timing_query_result_t {
    const auto row = absolute_row(measures, position);
    if (!row) {
        return {.error = error(timing_error_code_t::position_out_of_range,
                               "timing query position is outside chart geometry")};
    }
    return index.seconds_at(*row, side);
}

namespace {

auto selected_bpm_is_last_before_beat(const chart_document_t& chart,
                                      const measure_index_t& measures, object_id_t selected,
                                      std::uint64_t anchor_row, std::uint64_t beat_row) -> bool {
    for (const auto& event : chart.bpms) {
        if (event.id == selected) {
            continue;
        }
        const auto row = absolute_row(measures, event.position);
        if (!row || (*row >= anchor_row && *row <= beat_row)) {
            return false;
        }
    }
    return true;
}

auto locked_anchors_preserved(const chart_document_t& chart, const measure_index_t& measures,
                              const timing_index_t& fitted, std::uint64_t anchor_row,
                              std::uint32_t sample_rate) -> bool {
    for (const auto& anchor : chart.timing_anchors) {
        if (!anchor.locked) {
            continue;
        }
        const auto row = absolute_row(measures, anchor.position);
        if (!row || *row < anchor_row) {
            continue;
        }
        const auto at = fitted.seconds_at(*row);
        const auto frame =
            at ? sample_frame_at(*at.seconds, sample_rate, sample_rounding_t::nearest_ties_to_even)
               : sample_frame_result_t{};
        if (!frame || *frame.frame != anchor.source_sample_frame) {
            return false;
        }
    }
    return true;
}

struct beat_fit_selection_t {
    std::size_t selected_index{};
    std::uint64_t anchor_row{};
    std::uint64_t beat_row{};
};

auto beat_fit_selection(const chart_document_t& chart, const measure_index_t& measures,
                        object_id_t bpm_event, local_position_t beat)
    -> std::variant<beat_fit_selection_t, timing_diagnostic_t> {
    const auto selected = std::ranges::find(chart.bpms, bpm_event, &bpm_event_t::id);
    if (!bpm_event || selected == chart.bpms.end() ||
        std::ranges::count(chart.bpms, bpm_event, &bpm_event_t::id) != 1) {
        return error(timing_error_code_t::invalid_fit, "beat fit requires one selected BPM event");
    }
    const auto anchor_row = absolute_row(measures, selected->position);
    const auto beat_row = absolute_row(measures, beat);
    if (!anchor_row || !beat_row || *anchor_row >= *beat_row) {
        return error(timing_error_code_t::position_out_of_range,
                     "fitted beat must follow the selected BPM event");
    }
    if (!selected_bpm_is_last_before_beat(chart, measures, bpm_event, *anchor_row, *beat_row)) {
        return error(timing_error_code_t::invalid_fit,
                     "selected BPM event is not the last unambiguous change before beat");
    }
    return beat_fit_selection_t{static_cast<std::size_t>(selected - chart.bpms.begin()),
                                *anchor_row, *beat_row};
}

auto candidate_beat_time(chart_document_t& candidate, const beat_fit_selection_t& selected,
                         std::int64_t bpm) -> std::optional<signed_rational_t> {
    candidate.bpms[selected.selected_index].beats_per_minute = {bpm, 1};
    const auto built = build_timing_index(candidate);
    if (!built) {
        return std::nullopt;
    }
    const auto end = (*built.index).seconds_at(selected.beat_row);
    return end ? end.seconds : std::nullopt;
}

} // namespace

auto fit_beat_interval(const chart_document_t& chart, object_id_t bpm_event, local_position_t beat,
                       std::uint64_t target_sample_frame, std::uint32_t sample_rate)
    -> beat_interval_fit_result_t {
    const auto reject = [](timing_error_code_t code,
                           std::string message) -> beat_interval_fit_result_t {
        return {.error = error(code, std::move(message))};
    };
    if (sample_rate == 0 || target_sample_frame > maximum_i64_magnitude) {
        return reject(timing_error_code_t::invalid_fit,
                      "beat fit requires a representable target source frame and sample rate");
    }
    const auto measure_build = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(measure_build)) {
        return reject(timing_error_code_t::invalid_document,
                      "beat fit requires valid chart measure geometry");
    }
    const auto& measures = std::get<measure_index_t>(measure_build);
    const auto selection = beat_fit_selection(chart, measures, bpm_event, beat);
    if (const auto* failure = std::get_if<timing_diagnostic_t>(&selection)) {
        return {.error = *failure};
    }
    const auto& selected = std::get<beat_fit_selection_t>(selection);
    const auto original_build = build_timing_index(chart);
    if (!original_build) {
        return reject(timing_error_code_t::invalid_document,
                      "beat fit requires a valid timing index");
    }
    auto candidate = chart;
    const auto at_sixty = candidate_beat_time(candidate, selected, 60);
    const auto at_one_twenty = candidate_beat_time(candidate, selected, 120);
    const auto varying =
        at_sixty && at_one_twenty ? subtract_exact(*at_sixty, *at_one_twenty) : std::nullopt;
    const auto scaled = varying ? multiply_exact(*varying, {120, 1}) : std::nullopt;
    const auto twice_fast = at_one_twenty ? multiply_exact(*at_one_twenty, {2, 1}) : std::nullopt;
    const auto fixed =
        twice_fast && at_sixty ? subtract_exact(*twice_fast, *at_sixty) : std::nullopt;
    const auto target_time =
        signed_rational_t{static_cast<std::int64_t>(target_sample_frame), sample_rate};
    const auto available = fixed ? subtract_exact(target_time, *fixed) : std::nullopt;
    if (!scaled || !available) {
        return reject(timing_error_code_t::arithmetic_overflow,
                      "beat fit exceeds exact rational arithmetic bounds");
    }
    if (scaled->numerator <= 0 || available->numerator <= 0) {
        return reject(timing_error_code_t::invalid_fit,
                      "target frame leaves no positive BPM interval");
    }
    const auto fitted_bpm =
        positive_fraction({static_cast<std::uint64_t>(scaled->numerator), available->denominator},
                          {scaled->denominator, static_cast<std::uint64_t>(available->numerator)});
    if (!fitted_bpm || fitted_bpm->numerator <= 0) {
        return reject(timing_error_code_t::arithmetic_overflow,
                      "fitted BPM exceeds exact rational arithmetic bounds");
    }
    candidate.bpms[selected.selected_index].beats_per_minute = *fitted_bpm;
    const auto fitted_build = build_timing_index(candidate);
    if (!fitted_build) {
        return reject(timing_error_code_t::invalid_fit,
                      "fitted BPM does not produce a valid timing index");
    }
    const auto fitted_beat = (*fitted_build.index).seconds_at(selected.beat_row);
    const auto matched =
        fitted_beat ? compare_exact(*fitted_beat.seconds, target_time) : std::nullopt;
    if (!matched || *matched != 0) {
        return reject(timing_error_code_t::invalid_fit,
                      "fitted BPM does not reach the exact target frame");
    }
    if (!locked_anchors_preserved(chart, measures, *fitted_build.index, selected.anchor_row,
                                  sample_rate)) {
        return reject(timing_error_code_t::locked_anchor_conflict,
                      "beat fit would move a locked timing anchor");
    }
    return {.beats_per_minute = *fitted_bpm};
}

namespace {

auto extrapolated_row_before_chart(const timing_index_t& index, signed_rational_t query)
    -> timing_position_query_result_t {
    const auto elapsed = subtract_exact(query, index.rows.front().before_delays_seconds);
    const auto bpm = try_normalize(index.rows.front().effective_bpm);
    const auto row_magnitude =
        elapsed && elapsed->numerator < 0 && bpm && bpm->numerator > 0
            ? positive_fraction({magnitude(elapsed->numerator), magnitude(bpm->numerator),
                                 index.divisions_per_standard_measure},
                                {elapsed->denominator, bpm->denominator, 240})
            : std::nullopt;
    if (!row_magnitude || row_magnitude->numerator <= 0) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "pre-zero inverse timing extrapolation exceeds exact bounds")};
    }
    return {.absolute_row =
                signed_rational_t{-row_magnitude->numerator, row_magnitude->denominator}};
}

auto extrapolated_row_before_bounded_clock(const timing_index_t& index,
                                           signed_rational_t query)
    -> timing_position_query_result_t {
    const auto query_ticks = timing_arithmetic::clock_ticks(query);
    const auto reference_ticks = timing_arithmetic::clock_ticks(
        index.rows.front().before_delays_seconds);
    const auto bpm = try_normalize(index.rows.front().effective_bpm);
    if (!query_ticks || !reference_ticks || !bpm || bpm->numerator <= 0 ||
        index.divisions_per_standard_measure == 0) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "pre-zero bounded timing input exceeds clock bounds")};
    }
    if (*query_ticks >= *reference_ticks) return {.absolute_row = signed_rational_t{0, 1}};
    const auto elapsed_ticks = static_cast<std::uint64_t>(*reference_ticks) -
                               static_cast<std::uint64_t>(*query_ticks);
    const auto row_ticks = timing_arithmetic::rounded_factor_ratio(
        {elapsed_ticks, static_cast<std::uint64_t>(bpm->numerator),
         index.divisions_per_standard_measure, timing_arithmetic::row_quantum},
        {timing_arithmetic::clock_quantum, bpm->denominator, 240});
    if (!row_ticks || *row_ticks > maximum_i64_magnitude) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "pre-zero bounded inverse row exceeds the nanorow range")};
    }
    return {.absolute_row = try_normalize(
                {-static_cast<std::int64_t>(*row_ticks), timing_arithmetic::row_quantum})};
}

auto row_inside_exact_segment(const timing_index_t& index, const timing_segment_t& segment,
                              signed_rational_t query) -> timing_position_query_result_t {
    if (segment.warped) {
        return {.error = error(timing_error_code_t::invalid_document,
                               "audio time does not identify a row on a warp plateau")};
    }
    const auto delta_seconds = subtract_exact(query, segment.begin_seconds);
    if (!delta_seconds || delta_seconds->numerator < 0 || segment.effective_bpm.numerator <= 0 ||
        segment.effective_bpm.denominator == 0) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "inverse timing segment exceeds exact arithmetic bounds")};
    }
    const auto row_delta = positive_fraction(
        {static_cast<std::uint64_t>(delta_seconds->numerator), index.divisions_per_standard_measure,
         static_cast<std::uint64_t>(segment.effective_bpm.numerator)},
        {delta_seconds->denominator, 240, segment.effective_bpm.denominator});
    if (!row_delta || segment.begin_row > maximum_i64_magnitude) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "inverse musical row exceeds exact arithmetic bounds")};
    }
    const auto absolute = add_exact({static_cast<std::int64_t>(segment.begin_row), 1}, *row_delta);
    if (!absolute) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "inverse musical row exceeds exact arithmetic bounds")};
    }
    return {.absolute_row = *absolute};
}

auto row_inside_bounded_segment(const timing_segment_t& segment, signed_rational_t query)
    -> timing_position_query_result_t {
    if (segment.warped) {
        return {.error = error(timing_error_code_t::invalid_document,
                               "audio time does not identify a row on a warp plateau")};
    }
    const auto begin = timing_arithmetic::clock_ticks(segment.begin_seconds);
    const auto end = timing_arithmetic::clock_ticks(segment.end_seconds);
    const auto at = timing_arithmetic::clock_ticks(query);
    const auto row = begin && end && at ? timing_arithmetic::interpolate_row(
        segment.begin_row, segment.end_row, *begin, *end, *at) : std::nullopt;
    if (!row) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "bounded inverse row exceeds nanorow or clock range")};
    }
    return {.absolute_row = *row};
}

auto row_inside_segment(const timing_index_t& index, const timing_segment_t& segment,
                        signed_rational_t query) -> timing_position_query_result_t {
    return index.bounded_clock ? row_inside_bounded_segment(segment, query)
                               : row_inside_exact_segment(index, segment, query);
}

auto warp_position_at(const timing_index_t& index, signed_rational_t query,
                      timing_warp_inverse_side_t side)
    -> std::optional<timing_position_query_result_t> {
    const auto first_segment =
        std::lower_bound(index.segments.begin(), index.segments.end(), query,
                         [](const timing_segment_t& segment, signed_rational_t value) {
                             const auto comparison = compare_exact(segment.end_seconds, value);
                             return comparison && *comparison < 0;
                         });
    if (first_segment == index.segments.end()) {
        return std::nullopt;
    }
    const auto first_index = static_cast<std::size_t>(first_segment - index.segments.begin());
    auto warp_entry = std::optional<std::uint64_t>{};
    auto warp_exit = std::optional<std::uint64_t>{};
    for (auto segment_index = first_index; segment_index < index.segments.size(); ++segment_index) {
        const auto& segment = index.segments[segment_index];
        const auto begin_comparison = compare_exact(segment.begin_seconds, query);
        if (!begin_comparison || *begin_comparison > 0) {
            break;
        }
        if (!segment.warped) {
            continue;
        }
        const auto end_comparison = compare_exact(segment.end_seconds, query);
        if (!end_comparison) {
            return timing_position_query_result_t{
                .error = error(timing_error_code_t::invalid_document,
                               "timing index contains an invalid segment time")};
        }
        if (*begin_comparison == 0 && *end_comparison == 0) {
            if (!warp_entry) {
                warp_entry = segment.begin_row;
            }
            warp_exit = segment.end_row;
        }
    }
    if (!warp_entry || !warp_exit) {
        return std::nullopt;
    }
    const auto selected = side == timing_warp_inverse_side_t::entry ? *warp_entry : *warp_exit;
    if (selected > maximum_i64_magnitude) {
        return timing_position_query_result_t{
            .error = error(timing_error_code_t::arithmetic_overflow,
                           "inverse musical row exceeds exact position bounds")};
    }
    const auto row =
        std::ranges::lower_bound(index.rows, selected, {}, &timing_row_state_t::absolute_row);
    if (row == index.rows.end() || row->absolute_row != selected) {
        return timing_position_query_result_t{
            .error = error(timing_error_code_t::invalid_document,
                           "warp endpoint is missing from the timing index")};
    }
    return timing_position_query_result_t{
        .absolute_row = signed_rational_t{static_cast<std::int64_t>(selected), 1},
        .inside_warp = row->inside_warp};
}

struct prepared_inverse_query_t {
    signed_rational_t seconds;
    std::optional<timing_position_query_result_t> result;
};

auto prepare_inverse_query(const timing_index_t& index, signed_rational_t query,
                           timing_inverse_options_t options) -> prepared_inverse_query_t {
    if (index.divisions_per_standard_measure == 0 || index.rows.empty() ||
        index.segments.size() + 1 != index.rows.size() || index.rows.front().absolute_row != 0 ||
        index.rows.back().absolute_row != index.total_rows) {
        return {.result = timing_position_query_result_t{.error = error(
            timing_error_code_t::invalid_document,
            "inverse timing query requires a complete timing index")}};
    }
    const auto before = compare_exact(query, index.rows.front().before_delays_seconds);
    const auto after = compare_exact(query, index.rows.back().after_stops_seconds);
    if (!before || !after) {
        return {.result = timing_position_query_result_t{.error = error(
            timing_error_code_t::invalid_document,
            "timing index contains an invalid rational boundary")}};
    }
    if (*before < 0 && options.extrapolate_before_chart) {
        const auto position = index.bounded_clock
            ? extrapolated_row_before_bounded_clock(index, query)
            : extrapolated_row_before_chart(index, query);
        return {.result = position};
    }
    if (*before < 0 || *after > 0) {
        return {.result = timing_position_query_result_t{.error = error(
            timing_error_code_t::position_out_of_range,
            "audio time lies outside chart timing bounds")}};
    }
    if (index.bounded_clock) {
        const auto ticks = timing_arithmetic::clock_ticks(query);
        if (!ticks) {
            return {.result = timing_position_query_result_t{.error = error(
                timing_error_code_t::arithmetic_overflow,
                "inverse timing input exceeds the bounded clock range")}};
        }
        query = timing_arithmetic::clock_value(*ticks);
    }
    return {.seconds = query};
}

} // namespace

auto musical_position_at(const timing_index_t& index, signed_rational_t seconds,
                         timing_inverse_options_t options) -> timing_position_query_result_t {
    auto query = try_normalize(seconds);
    if (!query) {
        return {.error = error(timing_error_code_t::invalid_document,
                               "inverse timing query requires valid rational seconds")};
    }
    const auto prepared = prepare_inverse_query(index, *query, options);
    if (prepared.result) return *prepared.result;
    query = prepared.seconds;

    // A warp collapses every row in its half-open span to one audio time. Find
    // that plateau first so its endpoint choice is independent of neighboring
    // row pauses or segments.
    if (const auto warp = warp_position_at(index, *query, options.warp_side)) {
        return *warp;
    }

    const auto row =
        std::lower_bound(index.rows.begin(), index.rows.end(), *query,
                         [](const timing_row_state_t& boundary, signed_rational_t value) {
                             const auto comparison =
                                 compare_exact(boundary.after_stops_seconds, value);
                             return comparison && *comparison < 0;
                         });
    if (row == index.rows.end()) {
        return {.error = error(timing_error_code_t::position_out_of_range,
                               "audio time lies outside chart timing bounds")};
    }
    const auto arrival_comparison = compare_exact(*query, row->before_delays_seconds);
    const auto departure_comparison = compare_exact(*query, row->after_stops_seconds);
    if (!arrival_comparison || !departure_comparison) {
        return {.error = error(timing_error_code_t::invalid_document,
                               "timing index contains an invalid row time")};
    }
    if (*arrival_comparison >= 0 && *departure_comparison <= 0) {
        if (row->absolute_row > maximum_i64_magnitude) {
            return {.error = error(timing_error_code_t::arithmetic_overflow,
                                   "inverse musical row exceeds exact position bounds")};
        }
        return {.absolute_row = signed_rational_t{static_cast<std::int64_t>(row->absolute_row), 1},
                .inside_warp = row->inside_warp};
    }

    const auto row_index = static_cast<std::size_t>(row - index.rows.begin());
    if (*arrival_comparison < 0 && row_index > 0) {
        return row_inside_segment(index, index.segments[row_index - 1], *query);
    }
    return {.error = error(timing_error_code_t::position_out_of_range,
                           "audio time has no musical position in the timing index")};
}

auto sample_frame_at(signed_rational_t seconds, std::uint32_t sample_rate,
                     sample_rounding_t rounding) -> sample_frame_result_t {
    const auto value = try_normalize(seconds);
    if (!value || sample_rate == 0) {
        return {.error = error(timing_error_code_t::invalid_document,
                               "sample-frame conversion requires valid seconds and sample rate")};
    }
    if (value->numerator < 0) {
        return {.error = error(timing_error_code_t::negative_sample_frame,
                               "event occurs before audio frame zero")};
    }
    auto numerator = magnitude(value->numerator);
    auto denominator = value->denominator;
    const auto divisor = std::gcd<std::uint64_t>(sample_rate, denominator);
    const auto reduced_rate = sample_rate / divisor;
    denominator /= divisor;
    const auto product = checked_multiply(numerator, reduced_rate);
    auto whole = std::uint64_t{};
    auto remainder = std::uint64_t{};
    if (product) {
        whole = *product / denominator;
        remainder = *product % denominator;
    } else {
        const auto quotient = wide_math::divide(
            wide_math::multiply(numerator, reduced_rate), {0, denominator});
        if (!wide_math::fits_u64(quotient.quotient)) {
            return {.error = error(timing_error_code_t::arithmetic_overflow,
                                   "sample-frame conversion overflows")};
        }
        whole = quotient.quotient.low;
        remainder = quotient.remainder.low;
    }
    auto frame = whole;
    if (rounding == sample_rounding_t::ceil && remainder != 0) {
        if (frame == std::numeric_limits<std::uint64_t>::max()) {
            return {.error = error(timing_error_code_t::arithmetic_overflow,
                                   "rounded sample frame overflows")};
        }
        ++frame;
    } else if (rounding == sample_rounding_t::nearest_ties_to_even && remainder != 0) {
        const auto complement = denominator - remainder;
        const bool round_up = remainder > complement || (remainder == complement && frame % 2 != 0);
        if (round_up) {
            if (frame == std::numeric_limits<std::uint64_t>::max()) {
                return {.error = error(timing_error_code_t::arithmetic_overflow,
                                       "rounded sample frame overflows")};
            }
            ++frame;
        }
    }
    return {.frame = frame};
}

auto make_preview_music_event(resource_id_t resource, std::uint64_t music_frame_count)
    -> std::optional<preview_schedule_t::music_event_t> {
    if (!resource || music_frame_count == 0) return std::nullopt;
    return preview_schedule_t::music_event_t{resource, 0, 0, music_frame_count};
}

auto preview_music_source_frame_at(const preview_schedule_t::music_event_t& music,
                                  std::uint64_t timeline_frame)
    -> std::optional<std::uint64_t> {
    if (timeline_frame < music.sample_frame ||
        timeline_frame - music.sample_frame >= music.frame_count ||
        timeline_frame - music.sample_frame >
            std::numeric_limits<std::uint64_t>::max() - music.source_start_frame)
        return std::nullopt;
    return music.source_start_frame + timeline_frame - music.sample_frame;
}

namespace {

enum class scroll_speed_source_kind_t : std::uint8_t { scroll, speed };

struct positioned_scroll_speed_event_t {
    std::uint64_t row{};
    std::size_t insertion_order{};
    object_id_t id;
    std::size_t source_order{};
    scroll_speed_source_kind_t kind{scroll_speed_source_kind_t::scroll};
    const scroll_event_t* scroll{};
    const speed_event_t* speed{};
};

struct scroll_speed_event_context_t {
    const chart_document_t& chart;
    const timing_index_t& timing;
    const measure_index_t& measures;
    std::vector<timing_diagnostic_t>& diagnostics;
    std::vector<positioned_scroll_speed_event_t> events;
};

auto collect_scroll_events(scroll_speed_event_context_t& context) -> void {
    for (std::size_t order = 0; order < context.chart.scrolls.size(); ++order) {
        const auto& event = context.chart.scrolls[order];
        const auto row = absolute_row(context.measures, event.position);
        if (!row) {
            context.diagnostics.push_back(error(timing_error_code_t::position_out_of_range,
                                                "scroll event position is outside chart geometry",
                                                event.id));
            continue;
        }
        if (!try_normalize(event.multiplier)) {
            context.diagnostics.push_back(error(timing_error_code_t::invalid_scroll,
                                                "scroll multiplier must be a valid signed rational",
                                                event.id));
            continue;
        }
        const auto position_timing = context.timing.seconds_at(*row);
        if (!position_timing) {
            auto diagnostic = *position_timing.error;
            diagnostic.object = event.id;
            context.diagnostics.push_back(std::move(diagnostic));
            continue;
        }
        if (position_timing.inside_warp) {
            context.diagnostics.push_back(warning(
                timing_error_code_t::invalid_warp,
                "scroll event inside a half-open warp is suppressed by the scroll/speed evaluator",
                event.id));
            continue;
        }
        context.events.push_back({.row = *row,
                                  .insertion_order = order,
                                  .id = event.id,
                                  .source_order = event.provenance.source_order,
                                  .kind = scroll_speed_source_kind_t::scroll,
                                  .scroll = &event});
    }
}

auto collect_speed_events(scroll_speed_event_context_t& context) -> void {
    for (std::size_t order = 0; order < context.chart.speeds.size(); ++order) {
        const auto& event = context.chart.speeds[order];
        const auto row = absolute_row(context.measures, event.position);
        if (!row) {
            context.diagnostics.push_back(error(timing_error_code_t::position_out_of_range,
                                                "speed event position is outside chart geometry",
                                                event.id));
            continue;
        }
        if (event.multiplier.numerator < 0 || event.multiplier.denominator == 0 ||
            event.transition_beats.denominator == 0) {
            context.diagnostics.push_back(
                error(timing_error_code_t::invalid_speed,
                      "speed multiplier and duration must be bounded valid rationals", event.id));
            continue;
        }
        const auto position_timing = context.timing.seconds_at(*row);
        if (!position_timing) {
            auto diagnostic = *position_timing.error;
            diagnostic.object = event.id;
            context.diagnostics.push_back(std::move(diagnostic));
            continue;
        }
        if (position_timing.inside_warp) {
            context.diagnostics.push_back(warning(
                timing_error_code_t::invalid_warp,
                "speed event inside a half-open warp is suppressed by the scroll/speed evaluator",
                event.id));
            continue;
        }
        if (*row > maximum_i64_magnitude) {
            context.diagnostics.push_back(error(timing_error_code_t::arithmetic_overflow,
                                                "speed event row exceeds exact query bounds",
                                                event.id));
            continue;
        }
        context.events.push_back({.row = *row,
                                  .insertion_order = order,
                                  .id = event.id,
                                  .source_order = event.provenance.source_order,
                                  .kind = scroll_speed_source_kind_t::speed,
                                  .speed = &event});
    }
}

auto append_speed_segment(scroll_speed_event_context_t& context, scroll_speed_index_t& index,
                          const positioned_scroll_speed_event_t& positioned) -> bool {
    const auto query_time =
        context.timing.seconds_at(positioned.row, timing_boundary_side_t::event);
    if (!query_time) {
        auto diagnostic = *query_time.error;
        diagnostic.object = positioned.id;
        context.diagnostics.push_back(std::move(diagnostic));
        return false;
    }
    auto initial_multiplier = signed_rational_t{1, 1};
    if (!index.speed_segments.empty()) {
        const auto current = speed_segment_value(
            index.speed_segments.back(), {static_cast<std::int64_t>(positioned.row), 1},
            *query_time.seconds, context.chart.divisions_per_standard_measure);
        if (!current) {
            context.diagnostics.push_back(error(timing_error_code_t::arithmetic_overflow,
                                                "speed transition retargeting exceeds exact bounds",
                                                positioned.id));
            return false;
        }
        initial_multiplier = *current;
    }
    const auto& source = *positioned.speed;
    const auto target = try_normalize(
        {static_cast<std::int64_t>(source.multiplier.numerator), source.multiplier.denominator});
    if (!target) {
        context.diagnostics.push_back(error(timing_error_code_t::invalid_speed,
                                            "speed multiplier cannot be normalized",
                                            positioned.id));
        return false;
    }
    index.speed_segments.push_back({.begin_row = positioned.row,
                                    .end_row = context.measures.total_rows,
                                    .begin_seconds = *query_time.seconds,
                                    .initial_multiplier = initial_multiplier,
                                    .target_multiplier = *target,
                                    .transition_duration = source.transition_beats,
                                    .transition_is_seconds = source.transition_is_seconds,
                                    .source_event = positioned.id,
                                    .source_order = positioned.source_order});
    return true;
}

auto compile_scroll_speed_segments(scroll_speed_event_context_t& context,
                                   scroll_speed_index_t& index) -> bool {
    for (const auto& positioned : context.events) {
        if (positioned.kind == scroll_speed_source_kind_t::scroll) {
            index.scroll_segments.push_back(
                {.begin_row = positioned.row,
                 .end_row = context.measures.total_rows,
                 .multiplier = *try_normalize(positioned.scroll->multiplier),
                 .source_event = positioned.id,
                 .source_order = positioned.source_order});
            continue;
        }
        if (!append_speed_segment(context, index, positioned)) {
            return false;
        }
    }
    for (auto segment = std::size_t{}; segment < index.scroll_segments.size(); ++segment) {
        if (segment + 1 < index.scroll_segments.size()) {
            index.scroll_segments[segment].end_row = index.scroll_segments[segment + 1].begin_row;
        }
    }
    for (auto segment = std::size_t{}; segment < index.speed_segments.size(); ++segment) {
        if (segment + 1 < index.speed_segments.size()) {
            index.speed_segments[segment].end_row = index.speed_segments[segment + 1].begin_row;
        }
    }
    return true;
}

auto append_speed_completion_boundary(const scroll_speed_event_context_t& context,
                                      const speed_curve_segment_t& segment,
                                      std::vector<signed_rational_t>& boundaries) -> void {
    if (segment.transition_duration.numerator == 0) {
        return;
    }
    if (!segment.transition_is_seconds) {
        const auto transition_rows = positive_fraction(
            {segment.transition_duration.numerator, context.chart.divisions_per_standard_measure},
            {segment.transition_duration.denominator, 4});
        const auto end_row =
            transition_rows
                ? add_exact({static_cast<std::int64_t>(segment.begin_row), 1}, *transition_rows)
                : std::nullopt;
        if (end_row) {
            const auto in_chart = compare_row_to_integer(*end_row, context.measures.total_rows);
            if (in_chart && *in_chart < 0) {
                boundaries.push_back(*end_row);
            }
        }
        return;
    }
    auto numerator = segment.transition_duration.numerator;
    auto denominator = segment.transition_duration.denominator;
    const auto divisor = std::gcd(numerator, denominator);
    numerator /= divisor;
    denominator /= divisor;
    if (numerator > maximum_i64_magnitude) {
        return;
    }
    const auto completion_time =
        add_exact(segment.begin_seconds, {static_cast<std::int64_t>(numerator), denominator});
    if (!completion_time) {
        return;
    }
    const auto position = context.timing.row_at(*completion_time);
    if (!position || !position.absolute_row) {
        return;
    }
    const auto in_chart =
        compare_row_to_integer(*position.absolute_row, context.measures.total_rows);
    const auto after_start =
        compare_exact(*position.absolute_row, {static_cast<std::int64_t>(segment.begin_row), 1});
    if (in_chart && *in_chart < 0 && after_start && *after_start > 0) {
        boundaries.push_back(*position.absolute_row);
    }
}

auto build_displacement_boundaries(scroll_speed_event_context_t& context,
                                   const scroll_speed_index_t& index,
                                   scroll_speed_build_options_t options,
                                   std::vector<signed_rational_t>& boundaries) -> bool {
    boundaries.push_back({0, 1});
    boundaries.push_back({static_cast<std::int64_t>(context.measures.total_rows), 1});
    for (const auto& row : context.timing.rows) {
        boundaries.push_back({static_cast<std::int64_t>(row.absolute_row), 1});
    }
    for (const auto& segment : index.scroll_segments) {
        boundaries.push_back({static_cast<std::int64_t>(segment.begin_row), 1});
    }
    for (const auto& segment : index.speed_segments) {
        boundaries.push_back({static_cast<std::int64_t>(segment.begin_row), 1});
        append_speed_completion_boundary(context, segment, boundaries);
    }
    std::ranges::sort(boundaries, [](signed_rational_t left, signed_rational_t right) {
        const auto comparison = compare_exact(left, right);
        return comparison && *comparison < 0;
    });
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end(),
                                 [](signed_rational_t left, signed_rational_t right) {
                                     const auto comparison = compare_exact(left, right);
                                     return comparison && *comparison == 0;
                                 }),
                     boundaries.end());
    if (boundaries.size() < 2 || boundaries.size() - 1 > options.maximum_segments) {
        context.diagnostics.push_back(
            error(timing_error_code_t::capacity_exceeded,
                  "scroll displacement segment count exceeds the configured cap"));
        return false;
    }
    return true;
}

auto scroll_speed_components_at(scroll_speed_event_context_t& context,
                                const scroll_speed_index_t& index, signed_rational_t row,
                                timing_boundary_side_t side)
    -> std::optional<std::pair<signed_rational_t, signed_rational_t>> {
    const auto query_time = timing_seconds_at_row(context.timing, row, side);
    if (!query_time) {
        context.diagnostics.push_back(*query_time.error);
        return std::nullopt;
    }
    const auto before_event_side = side == timing_boundary_side_t::before_delays;
    const auto find_segment = [&](const auto& segments) {
        return std::upper_bound(
            segments.begin(), segments.end(), row,
            [&](signed_rational_t query, const auto& segment) {
                const auto comparison = compare_row_to_integer(query, segment.begin_row);
                return comparison && (*comparison < 0 || (*comparison == 0 && before_event_side));
            });
    };
    auto scroll = signed_rational_t{1, 1};
    const auto scroll_after = find_segment(index.scroll_segments);
    if (scroll_after != index.scroll_segments.begin()) {
        scroll = std::prev(scroll_after)->multiplier;
    }
    auto speed = signed_rational_t{1, 1};
    const auto speed_after = find_segment(index.speed_segments);
    if (speed_after != index.speed_segments.begin()) {
        const auto& segment = *std::prev(speed_after);
        const auto value = speed_segment_value(segment, row, *query_time.seconds,
                                               index.divisions_per_standard_measure);
        if (!value) {
            context.diagnostics.push_back(error(
                timing_error_code_t::arithmetic_overflow,
                "scroll displacement speed evaluation exceeds exact bounds", segment.source_event));
            return std::nullopt;
        }
        speed = *value;
    }
    return std::pair{scroll, speed};
}

auto append_displacement_segment(scroll_speed_event_context_t& context, scroll_speed_index_t& index,
                                 signed_rational_t begin_row, signed_rational_t end_row,
                                 signed_rational_t& cumulative_displacement) -> bool {
    const auto begin_components =
        scroll_speed_components_at(context, index, begin_row, timing_boundary_side_t::after_stops);
    const auto end_components =
        scroll_speed_components_at(context, index, end_row, timing_boundary_side_t::before_delays);
    if (!begin_components || !end_components) {
        return false;
    }
    const auto row_width = subtract_exact(end_row, begin_row);
    const auto midpoint_delta = row_width ? multiply_exact(*row_width, {1, 2}) : std::nullopt;
    const auto midpoint = midpoint_delta ? add_exact(begin_row, *midpoint_delta) : std::nullopt;
    const auto midpoint_time =
        midpoint ? timing_seconds_at_row(context.timing, *midpoint, timing_boundary_side_t::event)
                 : timing_query_result_t{};
    if (!midpoint_time) {
        if (midpoint_time.error) {
            context.diagnostics.push_back(*midpoint_time.error);
        } else {
            context.diagnostics.push_back(
                error(timing_error_code_t::arithmetic_overflow,
                      "scroll displacement midpoint exceeds exact bounds"));
        }
        return false;
    }
    const bool warped = midpoint_time.inside_warp;
    index.displacement_segments.push_back({.begin_row = begin_row,
                                           .end_row = end_row,
                                           .cumulative_begin_beats = cumulative_displacement,
                                           .scroll_multiplier = begin_components->first,
                                           .speed_begin_multiplier = begin_components->second,
                                           .warped = warped});
    if (warped) {
        return true;
    }
    const auto beat_width =
        positive_fraction({magnitude(row_width->numerator), 4},
                          {row_width->denominator, context.chart.divisions_per_standard_measure});
    const auto speed_sum = add_exact(begin_components->second, end_components->second);
    const auto average_speed = speed_sum ? multiply_exact(*speed_sum, {1, 2}) : std::nullopt;
    const auto average_effective =
        average_speed ? multiply_exact(begin_components->first, *average_speed) : std::nullopt;
    const auto displacement = average_effective && beat_width
                                  ? multiply_exact(*average_effective, *beat_width)
                                  : std::nullopt;
    const auto next_cumulative =
        displacement ? add_exact(cumulative_displacement, *displacement) : std::nullopt;
    if (!next_cumulative) {
        context.diagnostics.push_back(error(timing_error_code_t::arithmetic_overflow,
                                            "scroll displacement exceeds exact arithmetic bounds"));
        return false;
    }
    cumulative_displacement = *next_cumulative;
    return true;
}

auto compile_displacement_segments(scroll_speed_event_context_t& context,
                                   scroll_speed_index_t& index,
                                   const std::vector<signed_rational_t>& boundaries) -> bool {
    auto cumulative_displacement = signed_rational_t{};
    index.displacement_segments.reserve(boundaries.size() - 1);
    for (auto boundary = std::size_t{}; boundary + 1 < boundaries.size(); ++boundary) {
        if (!append_displacement_segment(context, index, boundaries[boundary],
                                         boundaries[boundary + 1], cumulative_displacement)) {
            return false;
        }
    }
    index.total_displacement_beats = cumulative_displacement;
    return true;
}

} // namespace

auto build_scroll_speed_index(const chart_document_t& chart, const timing_index_t& timing,
                              scroll_speed_build_options_t options) -> scroll_speed_build_result_t {
    scroll_speed_build_result_t result;
    const auto built_measures = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(built_measures)) {
        result.diagnostics.push_back(error(timing_error_code_t::invalid_document,
                                           "scroll/speed chart measure geometry is invalid"));
        return result;
    }
    const auto& measures = std::get<measure_index_t>(built_measures);
    if (timing.divisions_per_standard_measure != chart.divisions_per_standard_measure ||
        timing.total_rows != measures.total_rows || timing.rows.empty() ||
        timing.total_rows > maximum_i64_magnitude ||
        timing.source_fingerprint != timing_fingerprint(chart)) {
        result.diagnostics.push_back(
            error(timing_error_code_t::invalid_document,
                  "scroll/speed compilation requires a current timing index"));
        return result;
    }
    if (options.maximum_segments == 0) {
        result.diagnostics.push_back(
            error(timing_error_code_t::capacity_exceeded, "scroll/speed segment capacity is zero"));
        return result;
    }
    if (timing.rows.size() > options.maximum_timing_boundaries) {
        result.diagnostics.push_back(
            error(timing_error_code_t::capacity_exceeded,
                  "timing boundary count exceeds the scroll/speed evaluator cap"));
        return result;
    }
    if (chart.scrolls.size() > options.maximum_events ||
        chart.speeds.size() > options.maximum_events - chart.scrolls.size()) {
        result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                           "scroll/speed event count exceeds the configured cap"));
        return result;
    }
    if (chart.scrolls.size() >= options.maximum_segments ||
        chart.speeds.size() > options.maximum_segments - 1 - chart.scrolls.size()) {
        result.diagnostics.push_back(
            error(timing_error_code_t::capacity_exceeded,
                  "scroll/speed segment count exceeds the configured cap"));
        return result;
    }

    auto event_context = scroll_speed_event_context_t{chart, timing, measures, result.diagnostics};
    event_context.events.reserve(chart.scrolls.size() + chart.speeds.size());
    collect_scroll_events(event_context);
    collect_speed_events(event_context);
    auto& events = event_context.events;
    if (has_errors(result.diagnostics)) {
        return result;
    }
    std::ranges::stable_sort(events, [](const auto& left, const auto& right) {
        return std::tuple{left.row, left.source_order, left.insertion_order, left.id.value,
                          static_cast<std::uint8_t>(left.kind)} <
               std::tuple{right.row, right.source_order, right.insertion_order, right.id.value,
                          static_cast<std::uint8_t>(right.kind)};
    });

    scroll_speed_index_t index{
        .divisions_per_standard_measure = chart.divisions_per_standard_measure,
        .total_rows = measures.total_rows,
        .timing_source_fingerprint = timing.source_fingerprint,
        .source_fingerprint = scroll_speed_fingerprint(chart),
    };
    index.scroll_segments.reserve(chart.scrolls.size() + 1);
    index.speed_segments.reserve(chart.speeds.size());
    index.scroll_segments.push_back(
        {.begin_row = 0, .end_row = measures.total_rows, .multiplier = {1, 1}});
    if (!compile_scroll_speed_segments(event_context, index)) {
        return result;
    }

    std::vector<signed_rational_t> displacement_boundaries;
    if (!build_displacement_boundaries(event_context, index, options, displacement_boundaries)) {
        return result;
    }

    if (!compile_displacement_segments(event_context, index, displacement_boundaries)) {
        return result;
    }
    result.index = std::move(index);
    return result;
}

auto scroll_speed_index_matches(const scroll_speed_index_t& index, const chart_document_t& chart,
                                const timing_index_t& timing) -> bool {
    return index.divisions_per_standard_measure == chart.divisions_per_standard_measure &&
           timing.divisions_per_standard_measure == chart.divisions_per_standard_measure &&
           index.divisions_per_standard_measure == timing.divisions_per_standard_measure &&
           index.total_rows == timing.total_rows &&
           index.timing_source_fingerprint == timing.source_fingerprint &&
           timing.source_fingerprint == timing_fingerprint(chart) &&
           index.source_fingerprint == scroll_speed_fingerprint(chart);
}

namespace {

auto scroll_displacement_at(const scroll_speed_index_t& index, signed_rational_t row,
                            signed_rational_t speed_multiplier)
    -> std::variant<signed_rational_t, timing_diagnostic_t> {
    const auto at_chart_end = compare_row_to_integer(row, index.total_rows);
    if (!at_chart_end) {
        return error(timing_error_code_t::invalid_document,
                     "scroll/speed query row cannot be compared exactly");
    }
    if (*at_chart_end >= 0) {
        return index.total_displacement_beats;
    }
    const auto after_segment = std::upper_bound(
        index.displacement_segments.begin(), index.displacement_segments.end(), row,
        [](signed_rational_t query, const scroll_displacement_segment_t& segment) {
            const auto comparison = compare_exact(query, segment.begin_row);
            return comparison && *comparison < 0;
        });
    if (after_segment == index.displacement_segments.begin()) {
        return error(timing_error_code_t::invalid_document,
                     "scroll displacement index has no containing segment");
    }
    const auto& segment = *std::prev(after_segment);
    const auto begin_comparison = compare_exact(row, segment.begin_row);
    const auto end_comparison = compare_exact(row, segment.end_row);
    if (!begin_comparison || !end_comparison || *begin_comparison < 0 || *end_comparison > 0) {
        return error(timing_error_code_t::invalid_document,
                     "scroll displacement segment does not contain the query row");
    }
    auto displacement = segment.cumulative_begin_beats;
    if (segment.warped || *begin_comparison <= 0) {
        return displacement;
    }
    const auto row_distance = subtract_exact(row, segment.begin_row);
    const auto beat_distance =
        row_distance && row_distance->numerator >= 0
            ? positive_fraction({magnitude(row_distance->numerator), 4},
                                {row_distance->denominator, index.divisions_per_standard_measure})
            : std::nullopt;
    const auto speed_sum = add_exact(segment.speed_begin_multiplier, speed_multiplier);
    const auto average_speed = speed_sum ? multiply_exact(*speed_sum, {1, 2}) : std::nullopt;
    const auto average_effective =
        average_speed ? multiply_exact(segment.scroll_multiplier, *average_speed) : std::nullopt;
    const auto partial = average_effective && beat_distance
                             ? multiply_exact(*average_effective, *beat_distance)
                             : std::nullopt;
    const auto accumulated = partial ? add_exact(displacement, *partial) : std::nullopt;
    if (!accumulated) {
        return error(timing_error_code_t::arithmetic_overflow,
                     "scroll displacement query exceeds exact arithmetic bounds");
    }
    return *accumulated;
}

auto extrapolated_scroll_speed_before_chart(const scroll_speed_index_t& index,
                                            const timing_index_t& timing, signed_rational_t row)
    -> scroll_speed_query_result_t {
    const auto at_zero = scroll_speed_at(index, timing, {0, 1}, timing_boundary_side_t::event);
    const auto beat_distance = multiply_exact(row, {4, index.divisions_per_standard_measure});
    const auto displacement_delta =
        at_zero && beat_distance
            ? multiply_exact(*beat_distance, at_zero.state->effective_multiplier)
            : std::nullopt;
    const auto displacement =
        at_zero && displacement_delta
            ? add_exact(at_zero.state->displacement_beats, *displacement_delta)
            : std::nullopt;
    if (!at_zero || !displacement) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "pre-zero scroll extrapolation exceeds exact bounds")};
    }
    const auto render_displacement = to_double(*displacement);
    if (!std::isfinite(render_displacement)) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "pre-zero scroll displacement is not finite for rendering")};
    }
    auto state = *at_zero.state;
    state.displacement_beats = *displacement;
    state.render_displacement_beats = render_displacement;
    return {.state = state};
}

} // namespace

auto scroll_speed_at(const scroll_speed_index_t& index, const timing_index_t& timing,
                     signed_rational_t absolute_row, timing_boundary_side_t side,
                     bool extrapolate_before_chart) -> scroll_speed_query_result_t {
    if (index.divisions_per_standard_measure != timing.divisions_per_standard_measure ||
        index.total_rows != timing.total_rows ||
        index.timing_source_fingerprint != timing.source_fingerprint) {
        return {.error = error(timing_error_code_t::invalid_document,
                               "scroll/speed index does not match the timing index")};
    }
    const auto row = try_normalize(absolute_row);
    if (!row) {
        return {.error = error(timing_error_code_t::invalid_document,
                               "scroll/speed query requires a valid rational row")};
    }
    if (row->numerator < 0 && extrapolate_before_chart) {
        return extrapolated_scroll_speed_before_chart(index, timing, *row);
    }
    const auto query_time = timing_seconds_at_row(timing, *row, side);
    if (!query_time) {
        return {.error = *query_time.error};
    }
    const auto before_event_side = side == timing_boundary_side_t::before_delays;
    const auto find_segment = [&](const auto& segments) {
        return std::upper_bound(
            segments.begin(), segments.end(), *row,
            [&](signed_rational_t query, const auto& segment) {
                const auto comparison = compare_row_to_integer(query, segment.begin_row);
                return comparison && (*comparison < 0 || (*comparison == 0 && before_event_side));
            });
    };

    auto scroll_multiplier = signed_rational_t{1, 1};
    const auto scroll_after = find_segment(index.scroll_segments);
    if (scroll_after != index.scroll_segments.begin()) {
        scroll_multiplier = std::prev(scroll_after)->multiplier;
    }
    auto speed_multiplier = signed_rational_t{1, 1};
    const auto speed_after = find_segment(index.speed_segments);
    if (speed_after != index.speed_segments.begin()) {
        const auto& segment = *std::prev(speed_after);
        const auto value = speed_segment_value(segment, *row, *query_time.seconds,
                                               index.divisions_per_standard_measure);
        if (!value) {
            return {.error = error(timing_error_code_t::arithmetic_overflow,
                                   "speed curve evaluation exceeds exact arithmetic bounds",
                                   segment.source_event)};
        }
        speed_multiplier = *value;
    }
    const auto effective = multiply_exact(scroll_multiplier, speed_multiplier);
    if (!effective) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "effective scroll multiplier exceeds exact arithmetic bounds")};
    }
    const auto displacement_result = scroll_displacement_at(index, *row, speed_multiplier);
    if (const auto* failure = std::get_if<timing_diagnostic_t>(&displacement_result)) {
        return {.error = *failure};
    }
    const auto displacement = std::get<signed_rational_t>(displacement_result);
    const auto render_multiplier =
        to_double(*effective);
    const auto render_displacement =
        to_double(displacement);
    if (!std::isfinite(render_multiplier) || !std::isfinite(render_displacement)) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "scroll multiplier or displacement is not finite for rendering")};
    }
    return {.state = scroll_speed_state_t{.scroll_multiplier = scroll_multiplier,
                                          .speed_multiplier = speed_multiplier,
                                          .effective_multiplier = *effective,
                                          .displacement_beats = displacement,
                                          .render_multiplier = render_multiplier,
                                          .render_displacement_beats = render_displacement}};
}

namespace {

struct transition_span_context_t {
    const scroll_speed_index_t& index;
    const timing_index_t& timing;
    const speed_curve_segment_t& segment;
    signed_rational_t begin_row;
    signed_rational_t cap_row;
    signed_rational_t cap_seconds;
    bool replaced;
};

struct transition_span_end_t {
    signed_rational_t row;
    signed_rational_t seconds;
    bool clipped{};
};

auto seconds_transition_end(const transition_span_context_t& context)
    -> std::variant<transition_span_end_t, timing_diagnostic_t> {
    const auto& duration = context.segment.transition_duration;
    const auto available = subtract_exact(context.cap_seconds, context.segment.begin_seconds);
    if (!available || available->numerator < 0) {
        return error(timing_error_code_t::arithmetic_overflow,
                     "SPEEDS transition end exceeds exact timing bounds",
                     context.segment.source_event);
    }
    const auto comparison =
        compare_positive_fractions(duration.numerator, duration.denominator,
                                   magnitude(available->numerator), available->denominator);
    if (comparison >= 0) {
        return transition_span_end_t{context.cap_row, context.cap_seconds, comparison > 0};
    }
    auto numerator = duration.numerator;
    auto denominator = duration.denominator;
    const auto divisor = std::gcd(numerator, denominator);
    numerator /= divisor;
    denominator /= divisor;
    if (numerator > maximum_i64_magnitude) {
        return error(timing_error_code_t::arithmetic_overflow,
                     "SPEEDS transition duration exceeds exact timing bounds",
                     context.segment.source_event);
    }
    const auto natural_end = add_exact(context.segment.begin_seconds,
                                       {static_cast<std::int64_t>(numerator), denominator});
    if (!natural_end) {
        return error(timing_error_code_t::arithmetic_overflow,
                     "SPEEDS transition end exceeds exact timing bounds",
                     context.segment.source_event);
    }
    const auto position = context.timing.row_at(*natural_end);
    if (!position) {
        return error(position.error->code, position.error->message, context.segment.source_event);
    }
    return transition_span_end_t{*position.absolute_row, *natural_end, false};
}

auto rows_transition_end(const transition_span_context_t& context)
    -> std::variant<transition_span_end_t, timing_diagnostic_t> {
    const auto& duration = context.segment.transition_duration;
    const auto available_rows = subtract_exact(context.cap_row, context.begin_row);
    const auto available_beats =
        available_rows && available_rows->numerator >= 0
            ? positive_fraction(
                  {magnitude(available_rows->numerator), 4},
                  {available_rows->denominator, context.index.divisions_per_standard_measure})
            : std::nullopt;
    if (!available_rows || !available_beats) {
        return error(timing_error_code_t::arithmetic_overflow,
                     "SPEEDS transition span exceeds exact row bounds",
                     context.segment.source_event);
    }
    const auto comparison = compare_positive_fractions(duration.numerator, duration.denominator,
                                                       magnitude(available_beats->numerator),
                                                       available_beats->denominator);
    if (comparison > 0) {
        if (context.replaced) {
            return transition_span_end_t{context.cap_row, context.cap_seconds, true};
        }
        const auto arrival = timing_seconds_at_row(context.timing, context.cap_row,
                                                   timing_boundary_side_t::before_delays);
        if (!arrival) {
            return error(arrival.error->code, arrival.error->message, context.segment.source_event);
        }
        return transition_span_end_t{context.cap_row, *arrival.seconds, true};
    }
    const auto transition_rows =
        positive_fraction({duration.numerator, context.index.divisions_per_standard_measure},
                          {duration.denominator, 4});
    const auto natural_end =
        transition_rows ? add_exact(context.begin_row, *transition_rows) : std::nullopt;
    if (!natural_end) {
        return error(timing_error_code_t::arithmetic_overflow,
                     "SPEEDS transition end exceeds exact row bounds",
                     context.segment.source_event);
    }
    const auto arrival =
        timing_seconds_at_row(context.timing, *natural_end, timing_boundary_side_t::before_delays);
    if (!arrival) {
        return error(arrival.error->code, arrival.error->message, context.segment.source_event);
    }
    return transition_span_end_t{*natural_end, *arrival.seconds, false};
}

auto scroll_speed_transition_span_at(const scroll_speed_index_t& index,
                                     const timing_index_t& timing,
                                     std::vector<speed_curve_segment_t>::const_iterator segment)
    -> scroll_speed_transition_span_result_t {
    const auto source_event = segment->source_event;
    const auto span_error = [&](timing_error_code_t code, std::string message) {
        return scroll_speed_transition_span_result_t{
            .error = error(code, std::move(message), source_event)};
    };
    if (segment->begin_row > segment->end_row || segment->end_row > index.total_rows ||
        segment->begin_row > maximum_i64_magnitude ||
        segment->transition_duration.denominator == 0) {
        return span_error(timing_error_code_t::invalid_document,
                          "compiled SPEEDS transition span is invalid");
    }

    const auto begin_row = signed_rational_t{static_cast<std::int64_t>(segment->begin_row), 1};
    const auto begin_seconds = segment->begin_seconds;
    const auto duration = segment->transition_duration;
    if (duration.numerator == 0) {
        return {.span = scroll_speed_transition_span_t{.source_event = source_event,
                                                       .begin_row = begin_row,
                                                       .end_row = begin_row,
                                                       .begin_seconds = begin_seconds,
                                                       .end_seconds = begin_seconds,
                                                       .transition_is_seconds =
                                                           segment->transition_is_seconds,
                                                       .clipped = false}};
    }

    const auto next_segment = std::next(segment);
    const bool replaced = next_segment != index.speed_segments.end();
    const auto cap_row = signed_rational_t{static_cast<std::int64_t>(segment->end_row), 1};
    const auto cap_side =
        replaced ? timing_boundary_side_t::event
                 : (segment->transition_is_seconds ? timing_boundary_side_t::after_stops
                                                   : timing_boundary_side_t::before_delays);
    const auto cap_time = timing_seconds_at_row(timing, cap_row, cap_side);
    if (!cap_time) {
        return span_error(cap_time.error->code, cap_time.error->message);
    }

    const auto context = transition_span_context_t{
        index, timing, *segment, begin_row, cap_row, *cap_time.seconds, replaced};
    const auto end = segment->transition_is_seconds ? seconds_transition_end(context)
                                                    : rows_transition_end(context);
    if (const auto* failure = std::get_if<timing_diagnostic_t>(&end)) {
        return {.error = *failure};
    }
    const auto& resolved = std::get<transition_span_end_t>(end);

    return {.span = scroll_speed_transition_span_t{.source_event = source_event,
                                                   .begin_row = begin_row,
                                                   .end_row = resolved.row,
                                                   .begin_seconds = begin_seconds,
                                                   .end_seconds = resolved.seconds,
                                                   .transition_is_seconds =
                                                       segment->transition_is_seconds,
                                                   .clipped = resolved.clipped}};
}

auto scroll_speed_span_index_matches(const scroll_speed_index_t& index,
                                     const timing_index_t& timing) -> bool {
    return index.divisions_per_standard_measure == timing.divisions_per_standard_measure &&
           index.total_rows == timing.total_rows &&
           index.timing_source_fingerprint == timing.source_fingerprint;
}

} // namespace

auto scroll_speed_transition_span(const scroll_speed_index_t& index, const timing_index_t& timing,
                                  object_id_t source_event)
    -> scroll_speed_transition_span_result_t {
    const auto span_error = [&](timing_error_code_t code, std::string message) {
        return scroll_speed_transition_span_result_t{
            .error = error(code, std::move(message), source_event)};
    };
    if (!scroll_speed_span_index_matches(index, timing)) {
        return span_error(timing_error_code_t::invalid_document,
                          "scroll/speed index does not match the timing index");
    }
    const auto segment =
        std::ranges::find(index.speed_segments, source_event, &speed_curve_segment_t::source_event);
    if (segment == index.speed_segments.end()) {
        return span_error(timing_error_code_t::position_out_of_range,
                          "SPEEDS object is not present in the compiled index");
    }
    return scroll_speed_transition_span_at(index, timing, segment);
}

auto scroll_speed_transition_spans(const scroll_speed_index_t& index, const timing_index_t& timing,
                                   scroll_speed_transition_spans_options_t options)
    -> scroll_speed_transition_spans_result_t {
    scroll_speed_transition_spans_result_t result;
    if (!scroll_speed_span_index_matches(index, timing)) {
        result.diagnostics.push_back(error(timing_error_code_t::invalid_document,
                                           "scroll/speed index does not match the timing index"));
        return result;
    }

    const auto count = std::min(index.speed_segments.size(), options.maximum_spans);
    result.truncated = count < index.speed_segments.size();
    result.spans.reserve(count);
    const auto first = index.speed_segments.cbegin();
    for (std::size_t position = 0; position < count; ++position) {
        const auto span = scroll_speed_transition_span_at(
            index, timing, first + static_cast<std::ptrdiff_t>(position));
        if (!span.span) {
            result.diagnostics.push_back(
                span.error.value_or(error(timing_error_code_t::invalid_document,
                                          "SPEEDS transition span could not be built")));
            break;
        }
        result.spans.push_back(*span.span);
    }
    if (result.truncated) {
        result.diagnostics.push_back(
            warning(timing_error_code_t::capacity_exceeded,
                    "SPEEDS transition spans were truncated at the configured cap"));
    }
    return result;
}

namespace {

struct preview_input_counts_t {
    std::size_t playable{};
    std::size_t bgm{};
    bool playable_overflow{};
    bool bgm_overflow{};
    std::unordered_set<std::uint64_t> canonical_linked_playable_ids;
};

using linked_playable_group_t = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, note_kind_t,
                                           bool, std::uint64_t, std::uint64_t>;

auto linked_group_for(const note_t& note) -> linked_playable_group_t {
    return {note.position.measure.value,
            note.position.row,
            note.lane.value,
            note.kind,
            note.end.has_value(),
            note.end ? note.end->measure.value : 0,
            note.end ? note.end->row : 0};
}

auto count_preview_note(const note_t& note, preview_schedule_options_t options,
                        std::set<linked_playable_group_t>& linked_groups,
                        preview_input_counts_t& counts) -> void {
    if (!note.sound || (note.kind != note_kind_t::tap && note.kind != note_kind_t::hold &&
                        note.kind != note_kind_t::roll)) {
        return;
    }
    if (note.sound_channel_event && !linked_groups.insert(linked_group_for(note)).second) {
        return;
    }
    if (note.sound_channel_event) {
        counts.canonical_linked_playable_ids.insert(note.id.value);
    }
    if (counts.playable < options.maximum_playable_events) {
        ++counts.playable;
    } else {
        counts.playable_overflow = true;
    }
}

auto count_preview_inputs(const chart_document_t& chart, preview_schedule_options_t options)
    -> preview_input_counts_t {
    preview_input_counts_t counts;
    std::set<linked_playable_group_t> linked_playable_groups;
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            count_preview_note(note, options, linked_playable_groups, counts);
        }
        for (const auto& bgm : measure.bgm_events) {
            if (!bgm.sound) {
                continue;
            }
            if (counts.bgm < options.maximum_bgm_events) {
                ++counts.bgm;
            } else {
                counts.bgm_overflow = true;
            }
        }
    }
    return counts;
}

struct preview_metronome_context_t {
    const chart_document_t& chart;
    const timing_index_t& timing;
    const measure_index_t& measures;
    const preview_schedule_options_t& options;
    preview_schedule_t& schedule;
    std::vector<timing_diagnostic_t>& diagnostics;
};

struct preview_row_lookup_t {
    std::unordered_map<std::uint64_t, const measure_index_entry_t*> entries;

    explicit preview_row_lookup_t(const measure_index_t& measures) {
        entries.reserve(measures.entries.size());
        for (const auto& entry : measures.entries) {
            entries.emplace(entry.id.value, &entry);
        }
    }

    auto row_for(local_position_t position) const -> std::optional<std::uint64_t> {
        const auto entry = entries.find(position.measure.value);
        if (entry == entries.end() || position.row >= entry->second->row_capacity ||
            position.row >
                std::numeric_limits<std::uint64_t>::max() - entry->second->absolute_begin_row) {
            return std::nullopt;
        }
        return entry->second->absolute_begin_row + position.row;
    }
};

struct preview_event_context_t {
    const chart_document_t& chart;
    const timing_index_t& timing;
    const preview_schedule_options_t& options;
    const preview_input_counts_t& counts;
    const preview_row_lookup_t& rows;
    preview_schedule_t& schedule;
    std::vector<timing_diagnostic_t>& diagnostics;
};

auto append_preview_note(preview_event_context_t& context, const note_t& note) -> bool {
    if (!note.sound ||
        (note.kind != note_kind_t::tap && note.kind != note_kind_t::hold &&
         note.kind != note_kind_t::roll) ||
        (note.sound_channel_event &&
         !context.counts.canonical_linked_playable_ids.contains(note.id.value))) {
        return true;
    }
    const auto frame = position_frame(context.timing, context.rows.row_for(note.position),
                                      context.options, context.diagnostics, note.id);
    if (!frame) {
        return true;
    }
    if (context.schedule.playable.size() >= context.options.maximum_playable_events) {
        context.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                            "playable preview queue exceeds its configured cap",
                                            note.id));
        return false;
    }
    context.schedule.playable.push_back({.sample_frame = *frame,
                                         .object = note.id,
                                         .resource = *note.sound,
                                         .lane = note.lane,
                                         .source_order = note.provenance.source_order});
    return true;
}

auto append_preview_bgm(preview_event_context_t& context, const bgm_event_t& bgm) -> bool {
    if (!bgm.sound) {
        return true;
    }
    const auto frame = position_frame(context.timing, context.rows.row_for(bgm.position),
                                      context.options, context.diagnostics, bgm.id);
    if (!frame) {
        return true;
    }
    if (context.schedule.bgm.size() >= context.options.maximum_bgm_events) {
        context.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                            "BGM preview queue exceeds its configured cap",
                                            bgm.id));
        return false;
    }
    const auto lane =
        std::ranges::find(context.chart.bgm_source_lanes, bgm.source_lane, &bgm_source_lane_t::id);
    context.schedule.bgm.push_back({.sample_frame = *frame,
                                    .object = bgm.id,
                                    .resource = *bgm.sound,
                                    .source_order = lane == context.chart.bgm_source_lanes.end()
                                                        ? std::numeric_limits<std::size_t>::max()
                                                        : lane->source_order,
                                    .secondary_order = bgm.provenance.source_order});
    return true;
}

auto append_preview_bga(preview_event_context_t& context, const bga_event_t& bga) -> bool {
    const auto frame = position_frame(context.timing, context.rows.row_for(bga.position),
                                      context.options, context.diagnostics, bga.id);
    if (!frame) {
        return true;
    }
    if (context.schedule.bga.size() >= context.options.maximum_bga_events) {
        context.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                            "BGA preview queue exceeds its configured cap",
                                            bga.id));
        return false;
    }
    context.schedule.bga.push_back({.sample_frame = *frame,
                                    .object = bga.id,
                                    .resource = bga.resource,
                                    .kind = bga.kind,
                                    .draw_order = visual_draw_order(bga.kind),
                                    .source_order = bga.provenance.source_order});
    return true;
}

auto append_preview_events(preview_event_context_t& context) -> bool {
    for (const auto& measure : context.chart.measures) {
        for (const auto& note : measure.notes) {
            if (!append_preview_note(context, note)) {
                return false;
            }
        }
        for (const auto& bgm : measure.bgm_events) {
            if (!append_preview_bgm(context, bgm)) {
                return false;
            }
        }
    }
    for (const auto& bga : context.chart.bga_events) {
        if (!append_preview_bga(context, bga)) {
            return false;
        }
    }
    return true;
}

auto append_layer_tick(preview_event_context_t& context, local_position_t position,
                       object_id_t object, const std::vector<layer_id_t>& layers) -> bool {
    if (layers.empty()) return true;
    if (context.schedule.layer_ticks.size() >= context.options.maximum_layer_tick_events) {
        context.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
            "layer assist tick queue exceeds its configured cap", object));
        return false;
    }
    const auto frame = position_frame(context.timing, context.rows.row_for(position),
                                      context.options, context.diagnostics, object);
    if (frame) context.schedule.layer_ticks.push_back({*frame, layers});
    return true;
}

auto append_layer_ticks(preview_event_context_t& context) -> bool {
    if (!context.options.include_layer_ticks) return true;
    for (const auto& measure : context.chart.measures) {
        for (const auto& note : measure.notes) {
            const bool head = note.kind == note_kind_t::tap || note.kind == note_kind_t::hold ||
                              note.kind == note_kind_t::roll || note.kind == note_kind_t::lift;
            if (head && !append_layer_tick(context, note.position, note.id, note.semantic_layers))
                return false;
        }
        for (const auto& event : measure.bgm_events)
            if (!append_layer_tick(context, event.position, event.id, event.semantic_layers))
                return false;
    }
    return true;
}

auto append_all_preview_events(preview_event_context_t& context) -> bool {
    return append_preview_events(context) && append_layer_ticks(context);
}

auto metronome_accent_for(std::uint64_t pulse, bool compound) -> metronome_accent_t {
    return pulse == 0                   ? metronome_accent_t::measure
           : compound && pulse % 3 == 0 ? metronome_accent_t::group
                                        : metronome_accent_t::regular;
}

auto append_measure_metronome_pulses(preview_metronome_context_t& context,
                                     std::size_t measure_index, std::uint64_t pulses) -> bool {
    const auto& measure = context.chart.measures[measure_index];
    const auto& entry = context.measures.entries[measure_index];
    const auto step = measure.definition.meter
                          ? entry.row_capacity / pulses
                          : std::max<std::uint64_t>(1, context.chart.divisions_per_standard_measure / 4);
    const bool compound = measure.definition.meter && pulses > 3 && pulses % 3 == 0;
    for (std::uint64_t pulse = 0; pulse < pulses; ++pulse) {
        const auto row = entry.absolute_begin_row + pulse * step;
        const auto query = context.timing.seconds_at(row, timing_boundary_side_t::event);
        if (!query) {
            context.diagnostics.push_back(*query.error);
            continue;
        }
        if (query.inside_warp) {
            continue;
        }
        const auto frame =
            sample_frame_at(*query.seconds, context.options.sample_rate, context.options.rounding);
        if (!frame) {
            auto diagnostic = *frame.error;
            if (diagnostic.code == timing_error_code_t::negative_sample_frame) {
                diagnostic.severity = diagnostic_severity_t::warning;
            }
            context.diagnostics.push_back(std::move(diagnostic));
            continue;
        }
        if (context.schedule.metronome.size() >= context.options.maximum_metronome_events) {
            context.diagnostics.push_back(
                error(timing_error_code_t::capacity_exceeded,
                      "metronome preview queue exceeds its configured cap"));
            return false;
        }
        if (pulse > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            context.diagnostics.push_back(
                error(timing_error_code_t::arithmetic_overflow,
                      "metronome pulse index exceeds its signed representation"));
            return false;
        }
        context.schedule.metronome.push_back({.sample_frame = *frame.frame,
                                              .measure = measure.id,
                                              .pulse = static_cast<std::int64_t>(pulse),
                                              .accent = metronome_accent_for(pulse, compound)});
    }
    return true;
}

struct count_in_context_t {
    preview_metronome_context_t& preview;
    signed_rational_t pulse_seconds;
    signed_rational_t row_zero_seconds;
    std::uint64_t pulses;
    bool compound;
    bool reported_negative_time{};
};

auto append_count_in_pulse(count_in_context_t& context, std::uint64_t remaining) -> bool {
    const auto elapsed =
        multiply_exact(context.pulse_seconds, {static_cast<std::int64_t>(remaining), 1});
    const auto seconds =
        elapsed ? subtract_exact(context.row_zero_seconds, *elapsed) : std::nullopt;
    if (!seconds) {
        context.preview.diagnostics.push_back(error(timing_error_code_t::arithmetic_overflow,
                                                    "count-in time exceeds exact timing range"));
        return false;
    }
    const auto frame = sample_frame_at(*seconds, context.preview.options.sample_rate,
                                       context.preview.options.rounding);
    if (!frame) {
        auto diagnostic = *frame.error;
        if (diagnostic.code == timing_error_code_t::negative_sample_frame) {
            if (!context.reported_negative_time) {
                diagnostic.severity = diagnostic_severity_t::warning;
                diagnostic.message =
                    "one or more count-in pulses precede audio frame zero and were omitted";
                context.preview.diagnostics.push_back(std::move(diagnostic));
                context.reported_negative_time = true;
            }
            return true;
        }
        context.preview.diagnostics.push_back(std::move(diagnostic));
        return true;
    }
    if (context.preview.schedule.metronome.size() >=
        context.preview.options.maximum_metronome_events) {
        context.preview.diagnostics.push_back(
            error(timing_error_code_t::capacity_exceeded,
                  "metronome preview queue exceeds its configured cap"));
        return false;
    }
    const auto signed_pulse = -static_cast<std::int64_t>(remaining);
    const auto remainder = magnitude(signed_pulse) % context.pulses;
    const auto pulse_in_measure = remainder == 0 ? 0 : context.pulses - remainder;
    context.preview.schedule.metronome.push_back(
        {.sample_frame = *frame.frame,
         .measure = context.preview.chart.measures.front().id,
         .pulse = signed_pulse,
         .accent = metronome_accent_for(pulse_in_measure, context.compound)});
    return true;
}

auto append_count_in_pulses(preview_metronome_context_t& context,
                            std::optional<std::uint64_t> pulses) -> bool {
    if (context.options.count_in_pulses == 0 || !pulses) {
        return true;
    }
    const auto& first_measure = context.chart.measures.front();
    const auto step = first_measure.definition.meter
                          ? context.measures.entries.front().row_capacity / *pulses
                          : std::max<std::uint64_t>(1, context.chart.divisions_per_standard_measure / 4);
    const auto row_zero = context.timing.seconds_at(0, timing_boundary_side_t::event);
    const auto initial_bpm = context.timing.rows.empty()
                                 ? std::optional<signed_rational_t>{}
                                 : try_normalize(context.timing.rows.front().effective_bpm);
    if (!row_zero || !initial_bpm || initial_bpm->numerator <= 0) {
        context.diagnostics.push_back(
            row_zero.error.value_or(error(timing_error_code_t::invalid_document,
                                          "count-in requires a valid positive BPM at row zero")));
        return false;
    }
    const auto pulse_seconds =
        positive_fraction({step, 240, initial_bpm->denominator},
                          {context.chart.divisions_per_standard_measure,
                           static_cast<std::uint64_t>(initial_bpm->numerator)});
    if (!pulse_seconds) {
        context.diagnostics.push_back(error(timing_error_code_t::arithmetic_overflow,
                                            "count-in pulse duration exceeds exact timing range"));
        return false;
    }
    const bool compound = first_measure.definition.meter && *pulses > 3 && *pulses % 3 == 0;
    auto count_in =
        count_in_context_t{context, *pulse_seconds, *row_zero.seconds, *pulses, compound};
    for (auto remaining = context.options.count_in_pulses; remaining > 0; --remaining) {
        if (!append_count_in_pulse(count_in, remaining)) {
            return false;
        }
    }
    return true;
}

auto metronome_pulse_count(const preview_metronome_context_t& context, std::size_t measure_index)
    -> std::optional<std::uint64_t> {
    const auto& measure = context.chart.measures[measure_index];
    const auto& entry = context.measures.entries[measure_index];
    auto pulses = std::uint64_t{4};
    if (measure.definition.meter) {
        pulses = measure.definition.meter->numerator;
    } else {
        const auto quarter_rows =
            std::max<std::uint64_t>(1, context.chart.divisions_per_standard_measure / 4);
        return entry.row_capacity / quarter_rows + (entry.row_capacity % quarter_rows != 0);
    }
    if (pulses == 0 || entry.row_capacity % pulses != 0) {
        return std::nullopt;
    }
    return pulses;
}

auto append_metronome(preview_metronome_context_t& context) -> bool {
    if (context.options.count_in_pulses != 0 &&
        !append_count_in_pulses(context, metronome_pulse_count(context, 0))) {
        return false;
    }
    for (std::size_t measure_index = 0; measure_index < context.chart.measures.size();
         ++measure_index) {
        const auto pulses = metronome_pulse_count(context, measure_index);
        if (!pulses) {
            context.diagnostics.push_back(
                error(timing_error_code_t::invalid_document,
                      "measure meter cannot produce exact metronome pulse rows"));
            continue;
        }
        if (!append_measure_metronome_pulses(context, measure_index, *pulses)) {
            return false;
        }
    }
    return true;
}

struct preview_discontinuity_context_t {
    const chart_document_t& chart;
    const timing_index_t& timing;
    const preview_schedule_options_t& options;
    const preview_row_lookup_t& rows;
    preview_schedule_t& schedule;
    std::vector<timing_diagnostic_t>& diagnostics;
};

struct preview_timing_marker_t {
    local_position_t position;
    object_id_t object;
    std::size_t source_order;
    preview_discontinuity_kind_t kind;
    timing_boundary_side_t side;
};

auto append_timing_marker(preview_discontinuity_context_t& context, preview_timing_marker_t marker)
    -> void {
    const auto frame =
        boundary_frame(context.timing, context.rows.row_for(marker.position), marker.side,
                       context.options, context.diagnostics, marker.object);
    if (frame) {
        context.schedule.discontinuities.push_back({.sample_frame = *frame,
                                                    .kind = marker.kind,
                                                    .object = marker.object,
                                                    .source_order = marker.source_order,
                                                    .family_index = 0});
    }
}

auto append_chart_timing_markers(preview_discontinuity_context_t& context) -> void {
    for (const auto& bpm : context.chart.bpms) {
        const auto side =
            (context.schedule.discontinuity_profile == timing_discontinuity_profile_t::bms_legacy ||
             context.schedule.discontinuity_profile == timing_discontinuity_profile_t::bmson_1_0)
                ? timing_boundary_side_t::event
                : timing_boundary_side_t::before_delays;
        append_timing_marker(context, {bpm.position, bpm.id, bpm.provenance.source_order,
                                       preview_discontinuity_kind_t::bpm, side});
    }
    for (const auto& pause : context.chart.pauses) {
        const auto row = context.rows.row_for(pause.position);
        const auto row_state = row ? std::ranges::lower_bound(context.timing.rows, *row, {},
                                                              &timing_row_state_t::absolute_row)
                                   : context.timing.rows.end();
        if (row_state != context.timing.rows.end() && row_state->absolute_row == *row &&
            row_state->inside_warp) {
            continue;
        }
        const auto delay = pause.kind == pause_kind_t::delay;
        append_timing_marker(
            context,
            {pause.position, pause.id, pause.provenance.source_order,
             delay ? preview_discontinuity_kind_t::delay : preview_discontinuity_kind_t::stop,
             delay ? timing_boundary_side_t::before_delays : timing_boundary_side_t::event});
    }
}

} // namespace

auto build_preview_schedule(const chart_document_t& chart, const timing_index_t& timing,
                            preview_schedule_options_t options) -> preview_schedule_result_t {
    preview_schedule_result_t result;
    if (options.sample_rate == 0 ||
        timing.divisions_per_standard_measure != chart.divisions_per_standard_measure ||
        !valid_timing_discontinuity_profile(timing.discontinuity_profile)) {
        result.diagnostics.push_back(
            error(timing_error_code_t::invalid_document,
                  "preview schedule and timing index must use the same positive chart resolution"));
        return result;
    }
    const auto built_measures = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(built_measures)) {
        result.diagnostics.push_back(error(timing_error_code_t::invalid_document,
                                           "preview chart measure geometry is invalid"));
        return result;
    }
    const auto& measures = std::get<measure_index_t>(built_measures);
    if (measures.total_rows != timing.total_rows) {
        result.diagnostics.push_back(error(timing_error_code_t::invalid_document,
                                           "preview timing index is stale for this chart"));
        return result;
    }
    if (timing.source_fingerprint != timing_fingerprint(chart)) {
        result.diagnostics.push_back(error(timing_error_code_t::invalid_document,
                                           "preview timing index is stale for this chart"));
        return result;
    }
    preview_schedule_t schedule{.sample_rate = options.sample_rate,
                                .discontinuity_profile = timing.discontinuity_profile,
                                .playable = {},
                                .bgm = {},
                                .bga = {},
                                .metronome = {},
                                .discontinuities = {}};
    const auto input_counts = count_preview_inputs(chart, options);
    if (input_counts.playable_overflow || input_counts.bgm_overflow ||
        chart.bga_events.size() > options.maximum_bga_events ||
        (options.include_metronome && chart.measures.size() > options.maximum_metronome_events)) {
        result.diagnostics.push_back(
            error(timing_error_code_t::capacity_exceeded,
                  "preview input count exceeds a configured queue construction cap"));
        return result;
    }
    if (options.include_metronome && options.count_in_pulses > options.maximum_metronome_events) {
        result.diagnostics.push_back(
            error(timing_error_code_t::capacity_exceeded,
                  "count-in exceeds the metronome queue construction cap"));
        return result;
    }
    schedule.playable.reserve(input_counts.playable);
    schedule.bgm.reserve(input_counts.bgm);
    schedule.bga.reserve(std::min(chart.bga_events.size(), options.maximum_bga_events));
    const auto row_lookup = preview_row_lookup_t{measures};

    auto event_context = preview_event_context_t{
        chart, timing, options, input_counts, row_lookup, schedule, result.diagnostics};
    if (!append_all_preview_events(event_context)) {
        return result;
    }

    if (options.include_metronome) {
        auto metronome_context = preview_metronome_context_t{chart,   timing,   measures,
                                                             options, schedule, result.diagnostics};
        if (!append_metronome(metronome_context)) {
            return result;
        }
    }

    if (schedule.playable.size() > options.maximum_playable_events ||
        schedule.bgm.size() > options.maximum_bgm_events ||
        schedule.bga.size() > options.maximum_bga_events ||
        schedule.metronome.size() > options.maximum_metronome_events) {
        result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                           "preview event count exceeds a configured queue cap"));
    }
    if (has_errors(result.diagnostics)) {
        return result;
    }

    std::ranges::stable_sort(schedule.playable, [](const auto& left, const auto& right) {
        return std::tie(left.sample_frame, left.source_order, left.object.value) <
               std::tie(right.sample_frame, right.source_order, right.object.value);
    });
    std::ranges::stable_sort(schedule.bgm, [](const auto& left, const auto& right) {
        return std::tie(left.sample_frame, left.source_order, left.secondary_order,
                        left.object.value) < std::tie(right.sample_frame, right.source_order,
                                                      right.secondary_order, right.object.value);
    });
    std::ranges::stable_sort(schedule.bga, [](const auto& left, const auto& right) {
        return std::tie(left.sample_frame, left.draw_order, left.source_order, left.object.value) <
               std::tie(right.sample_frame, right.draw_order, right.source_order,
                        right.object.value);
    });
    std::ranges::stable_sort(schedule.metronome, {}, &preview_metronome_event_t::sample_frame);

    const auto activation_count = schedule.playable.size() + schedule.bgm.size() +
                                  schedule.bga.size() + schedule.metronome.size();
    if (activation_count > options.maximum_discontinuity_events ||
        chart.bpms.size() > options.maximum_discontinuity_events - activation_count ||
        chart.pauses.size() >
            options.maximum_discontinuity_events - activation_count - chart.bpms.size()) {
        result.diagnostics.push_back(
            error(timing_error_code_t::capacity_exceeded,
                  "preview discontinuity stream exceeds its configured cap"));
        return result;
    }
    schedule.discontinuities.reserve(activation_count + chart.bpms.size() + chart.pauses.size());
    const auto append_family = [&](const auto& events, preview_discontinuity_kind_t kind) {
        for (std::size_t index = 0; index < events.size(); ++index) {
            const auto& event = events[index];
            schedule.discontinuities.push_back({.sample_frame = event.sample_frame,
                                                .kind = kind,
                                                .object = event.object,
                                                .source_order = event.source_order,
                                                .family_index = index});
        }
    };
    append_family(schedule.playable, preview_discontinuity_kind_t::playable);
    append_family(schedule.bgm, preview_discontinuity_kind_t::bgm);
    append_family(schedule.bga, preview_discontinuity_kind_t::bga);
    for (std::size_t index = 0; index < schedule.metronome.size(); ++index) {
        const auto& event = schedule.metronome[index];
        schedule.discontinuities.push_back({.sample_frame = event.sample_frame,
                                            .kind = preview_discontinuity_kind_t::metronome,
                                            .object = {},
                                            .source_order = index,
                                            .family_index = index});
    }
    auto discontinuity_context = preview_discontinuity_context_t{
        chart, timing, options, row_lookup, schedule, result.diagnostics};
    append_chart_timing_markers(discontinuity_context);
    std::ranges::stable_sort(schedule.discontinuities, [&](const auto& left, const auto& right) {
        return std::tuple{left.sample_frame,
                          timing_discontinuity_order(schedule.discontinuity_profile, left.kind),
                          left.source_order,
                          static_cast<std::uint8_t>(left.kind),
                          left.object.value,
                          left.family_index} <
               std::tuple{right.sample_frame,
                          timing_discontinuity_order(schedule.discontinuity_profile, right.kind),
                          right.source_order,
                          static_cast<std::uint8_t>(right.kind),
                          right.object.value,
                          right.family_index};
    });
    if (has_errors(result.diagnostics)) {
        return result;
    }
    result.schedule = std::move(schedule);
    return result;
}


auto timing_index_t::seconds_at(std::uint64_t row, timing_boundary_side_t side) const
    -> timing_query_result_t { return timing_at(*this, row, side); }
auto timing_index_t::seconds_at(exact_chart_row_t row, timing_boundary_side_t side) const
    -> timing_query_result_t { return timing_at(*this, row, side); }
auto timing_index_t::seconds_at(const measure_index_t& measures, local_position_t position,
    timing_boundary_side_t side) const -> timing_query_result_t {
    return timing_at(*this, measures, position, side);
}
auto timing_index_t::row_at(signed_rational_t seconds, timing_inverse_options_t options) const
    -> timing_position_query_result_t { return musical_position_at(*this, seconds, options); }

auto timing_index_t::tail_seconds_per_row() const -> std::optional<double> {
    if (rows.empty() || divisions_per_standard_measure == 0) return std::nullopt;
    const auto bpm = rows.back().effective_bpm;
    if (bpm.numerator <= 0 || bpm.denominator == 0) return std::nullopt;
    return 240.0 * static_cast<double>(bpm.denominator) / bpm.numerator /
           divisions_per_standard_measure;
}

auto timing_index_t::seconds_at_approx(std::uint64_t row, chart_end_policy_t policy,
    timing_boundary_side_t side) const -> std::optional<double> {
    if (policy == chart_end_policy_t::clamp_to_end) row = std::min(row, total_rows);
    if (policy == chart_end_policy_t::clamp_to_last_row) {
        if (total_rows == 0) return std::nullopt;
        row = std::min(row, total_rows - 1);
    }
    if (row <= total_rows) {
        const auto query = seconds_at(row, side);
        if (!query.seconds || query.seconds->denominator == 0) return std::nullopt;
        return static_cast<double>(query.seconds->numerator) / query.seconds->denominator;
    }
    if (policy != chart_end_policy_t::extrapolate_tail) return std::nullopt;
    const auto end = seconds_at_approx(total_rows, chart_end_policy_t::reject,
                                      timing_boundary_side_t::after_stops);
    const auto rate = tail_seconds_per_row();
    if (!end || !rate) return std::nullopt;
    const auto value = *end + static_cast<double>(row - total_rows) * *rate;
    return std::isfinite(value) ? std::optional{value} : std::nullopt;
}

namespace {
using wide_unsigned_t = unsigned __int128;
using wide_signed_t = __int128;

auto gcd_wide(wide_unsigned_t left, wide_unsigned_t right) -> wide_unsigned_t {
    while (right != 0) {
        const auto remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

auto wide_magnitude(wide_signed_t value) -> wide_unsigned_t {
    return value < 0 ? static_cast<wide_unsigned_t>(-(value + 1)) + 1
                     : static_cast<wide_unsigned_t>(value);
}

auto multiply_wide(wide_unsigned_t left, wide_unsigned_t right)
    -> std::optional<wide_unsigned_t> {
    constexpr auto maximum = static_cast<wide_unsigned_t>(-1);
    if (right != 0 && left > maximum / right) return std::nullopt;
    return left * right;
}

auto tail_delta(const timing_index_t& timing, signed_rational_t seconds)
    -> std::optional<std::pair<wide_unsigned_t, wide_unsigned_t>> {
    if (timing.rows.empty() || seconds.denominator == 0)
        return std::nullopt;
    const auto& end = timing.rows.back().after_stops_seconds;
    if (end.denominator == 0) return std::nullopt;
    const auto common = std::gcd(seconds.denominator, end.denominator);
    const auto seconds_scale = end.denominator / common;
    const auto end_scale = seconds.denominator / common;
    const auto seconds_scaled = static_cast<wide_signed_t>(seconds.numerator) * seconds_scale;
    const auto end_scaled = static_cast<wide_signed_t>(end.numerator) * end_scale;
    wide_signed_t difference{};
    if (__builtin_sub_overflow(seconds_scaled, end_scaled, &difference) || difference <= 0)
        return std::nullopt;
    auto delta_denominator = multiply_wide(seconds.denominator, seconds_scale);
    if (!delta_denominator) return std::nullopt;
    auto delta_numerator = wide_magnitude(difference);
    const auto delta_gcd = gcd_wide(delta_numerator, *delta_denominator);
    delta_numerator /= delta_gcd;
    *delta_denominator /= delta_gcd;

    return std::pair{delta_numerator, *delta_denominator};
}

auto tail_row_delta(const timing_index_t& timing, signed_rational_t seconds)
    -> std::optional<std::pair<wide_unsigned_t, wide_unsigned_t>> {
    const auto delta = tail_delta(timing, seconds);
    if (!delta || timing.divisions_per_standard_measure == 0) return std::nullopt;
    const auto& bpm = timing.rows.back().effective_bpm;
    if (bpm.numerator <= 0 || bpm.denominator == 0) return std::nullopt;
    wide_unsigned_t numerator_factors[]{delta->first,
        static_cast<wide_unsigned_t>(bpm.numerator), timing.divisions_per_standard_measure};
    wide_unsigned_t denominator_factors[]{delta->second, bpm.denominator, 240};
    for (auto& numerator_factor : numerator_factors) {
        for (auto& denominator_factor : denominator_factors) {
            const auto factor_gcd = gcd_wide(numerator_factor, denominator_factor);
            numerator_factor /= factor_gcd;
            denominator_factor /= factor_gcd;
        }
    }
    wide_unsigned_t row_numerator = 1;
    wide_unsigned_t row_denominator = 1;
    for (const auto factor : numerator_factors) {
        const auto product = multiply_wide(row_numerator, factor);
        if (!product) return std::nullopt;
        row_numerator = *product;
    }
    for (const auto factor : denominator_factors) {
        const auto product = multiply_wide(row_denominator, factor);
        if (!product) return std::nullopt;
        row_denominator = *product;
    }
    return std::pair{row_numerator, row_denominator};
}

auto tail_row_at_seconds(const timing_index_t& timing, signed_rational_t seconds)
    -> std::optional<signed_rational_t> {
    const auto delta = tail_row_delta(timing, seconds);
    if (!delta) return std::nullopt;
    auto [row_numerator, row_denominator] = *delta;
    const auto row_gcd = gcd_wide(row_numerator, row_denominator);
    row_numerator /= row_gcd;
    row_denominator /= row_gcd;
    const auto absolute_begin = multiply_wide(timing.total_rows, row_denominator);
    if (!absolute_begin || row_numerator > static_cast<wide_unsigned_t>(-1) - *absolute_begin)
        return std::nullopt;
    auto absolute_numerator = *absolute_begin + row_numerator;
    auto absolute_denominator = row_denominator;
    const auto absolute_gcd = gcd_wide(absolute_numerator, absolute_denominator);
    absolute_numerator /= absolute_gcd;
    absolute_denominator /= absolute_gcd;
    if (absolute_numerator > static_cast<wide_unsigned_t>(std::numeric_limits<std::int64_t>::max()) ||
        absolute_denominator > std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
    return signed_rational_t{static_cast<std::int64_t>(absolute_numerator),
                             static_cast<std::uint64_t>(absolute_denominator)};
}

} // namespace

auto timing_index_t::row_at(signed_rational_t seconds, chart_end_policy_t policy,
    timing_inverse_options_t options) const -> timing_position_query_result_t {
    auto query = row_at(seconds, options);
    if (policy == chart_end_policy_t::reject) return query;
    if (query.absolute_row) {
        if (policy == chart_end_policy_t::clamp_to_last_row && total_rows != 0 &&
            total_rows - 1 <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            const auto end = signed_rational_t{static_cast<std::int64_t>(total_rows - 1), 1};
            const auto comparison = compare_exact(*query.absolute_row, end);
            if (comparison && *comparison > 0) query.absolute_row = end;
        }
        return query;
    }
    if (!query.error || query.error->code != timing_error_code_t::position_out_of_range ||
        rows.empty() || seconds.denominator == 0) return query;
    const auto end = compare_exact(seconds, rows.back().after_stops_seconds);
    if (!end) return query;
    if (*end >= 0 && policy == chart_end_policy_t::extrapolate_tail) {
        const auto row = tail_row_at_seconds(*this, seconds);
        return row ? timing_position_query_result_t{.absolute_row = *row} : query;
    }
    if (policy != chart_end_policy_t::clamp_to_end &&
        policy != chart_end_policy_t::clamp_to_last_row) return query;
    if (total_rows == 0 || (*end < 0 && policy == chart_end_policy_t::clamp_to_last_row)) return query;
    const auto limit = policy == chart_end_policy_t::clamp_to_end ? total_rows : total_rows - 1;
    if (limit > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return query;
    return {.absolute_row = signed_rational_t{*end >= 0 ? static_cast<std::int64_t>(limit) : 0, 1}};
}

auto timing_index_t::row_at_approx(signed_rational_t seconds, chart_end_policy_t policy,
    timing_inverse_options_t options) const -> std::optional<double> {
    const auto query = row_at(seconds, policy, options);
    if (!query.absolute_row || query.absolute_row->denominator == 0) return std::nullopt;
    const auto value = static_cast<double>(query.absolute_row->numerator) /
                       query.absolute_row->denominator;
    return std::isfinite(value) ? std::optional{value} : std::nullopt;
}

auto timing_index_t::row_at_frame(std::uint64_t frame, std::uint32_t sample_rate,
    chart_end_policy_t policy, timing_inverse_options_t options) const -> std::optional<double> {
    if (sample_rate == 0 || frame > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    // Playback's editable-row clamp only extends a nonnegative audio tail.
    if (policy == chart_end_policy_t::clamp_to_last_row && !rows.empty() &&
        rows.back().after_stops_seconds.numerator < 0) return std::nullopt;
    return row_at_approx({static_cast<std::int64_t>(frame), sample_rate}, policy, options);
}
} // namespace wormedit
