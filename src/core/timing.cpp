#include <wormedit/core/timing.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <ranges>
#include <set>
#include <tuple>
#include <unordered_map>
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

auto magnitude(std::int64_t value) noexcept -> std::uint64_t {
    if (value >= 0) {
        return static_cast<std::uint64_t>(value);
    }
    return static_cast<std::uint64_t>(-(value + 1)) + 1;
}

auto checked_multiply(std::uint64_t left, std::uint64_t right) -> std::optional<std::uint64_t> {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return std::nullopt;
    }
    return left * right;
}

auto checked_signed_multiply(std::int64_t value, std::uint64_t factor)
    -> std::optional<std::int64_t> {
    const auto absolute = magnitude(value);
    const auto limit = value < 0 ? minimum_i64_magnitude : maximum_i64_magnitude;
    if (absolute != 0 && factor > limit / absolute) {
        return std::nullopt;
    }
    const auto product = absolute * factor;
    if (value < 0 && product == minimum_i64_magnitude) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return value < 0 ? -static_cast<std::int64_t>(product) : static_cast<std::int64_t>(product);
}

auto normalized(signed_rational_t value) -> std::optional<signed_rational_t> {
    if (value.denominator == 0) {
        return std::nullopt;
    }
    const auto divisor = std::gcd(magnitude(value.numerator), value.denominator);
    const auto reduced_magnitude = magnitude(value.numerator) / divisor;
    if (value.numerator < 0 && reduced_magnitude == minimum_i64_magnitude) {
        value.numerator = std::numeric_limits<std::int64_t>::min();
    } else {
        value.numerator = value.numerator < 0 ? -static_cast<std::int64_t>(reduced_magnitude)
                                              : static_cast<std::int64_t>(reduced_magnitude);
    }
    value.denominator /= divisor;
    return value;
}

auto add_exact(signed_rational_t left, signed_rational_t right)
    -> std::optional<signed_rational_t> {
    const auto left_normal = normalized(left);
    const auto right_normal = normalized(right);
    if (!left_normal || !right_normal) {
        return std::nullopt;
    }
    const auto common = std::gcd(left_normal->denominator, right_normal->denominator);
    const auto left_scale = right_normal->denominator / common;
    const auto right_scale = left_normal->denominator / common;
    const auto scaled_left = checked_signed_multiply(left_normal->numerator, left_scale);
    const auto scaled_right = checked_signed_multiply(right_normal->numerator, right_scale);
    const auto denominator = checked_multiply(left_normal->denominator, left_scale);
    if (!scaled_left || !scaled_right || !denominator) {
        return std::nullopt;
    }
    if ((*scaled_right > 0 &&
         *scaled_left > std::numeric_limits<std::int64_t>::max() - *scaled_right) ||
        (*scaled_right < 0 &&
         *scaled_left < std::numeric_limits<std::int64_t>::min() - *scaled_right)) {
        return std::nullopt;
    }
    return normalized({*scaled_left + *scaled_right, *denominator});
}

auto subtract_exact(signed_rational_t left, signed_rational_t right)
    -> std::optional<signed_rational_t> {
    const auto left_normal = normalized(left);
    const auto right_normal = normalized(right);
    if (!left_normal || !right_normal) {
        return std::nullopt;
    }
    const auto common = std::gcd(left_normal->denominator, right_normal->denominator);
    const auto left_scale = right_normal->denominator / common;
    const auto right_scale = left_normal->denominator / common;
    const auto scaled_left = checked_signed_multiply(left_normal->numerator, left_scale);
    const auto scaled_right = checked_signed_multiply(right_normal->numerator, right_scale);
    const auto denominator = checked_multiply(left_normal->denominator, left_scale);
    if (!scaled_left || !scaled_right || !denominator ||
        (*scaled_right > 0 &&
         *scaled_left < std::numeric_limits<std::int64_t>::min() + *scaled_right) ||
        (*scaled_right < 0 &&
         *scaled_left > std::numeric_limits<std::int64_t>::max() + *scaled_right)) {
        return std::nullopt;
    }
    return normalized({*scaled_left - *scaled_right, *denominator});
}

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
    const auto left_normal = normalized(left);
    const auto right_normal = normalized(right);
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
    return normalized({static_cast<std::int64_t>(numerator), denominator});
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
    const auto left_normal = normalized(left);
    const auto right_normal = normalized(right);
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
    return normalized(*result);
}

auto rational_progress(signed_rational_t elapsed, rational_t duration)
    -> std::optional<signed_rational_t> {
    const auto value = normalized(elapsed);
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
    const auto value = normalized(row);
    if (!value) {
        return std::nullopt;
    }
    if (value->numerator < 0) {
        return -1;
    }
    return compare_positive_fractions(magnitude(value->numerator), value->denominator, integer, 1);
}

auto timing_seconds_at_row(const timing_index_t& timing, signed_rational_t absolute_row,
                           timing_boundary_side_t side) -> timing_query_result_t {
    const auto row = normalized(absolute_row);
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
    if (segment.warped) {
        return {.seconds = segment.begin_seconds, .inside_warp = true};
    }
    const auto offset = subtract_exact(*row, {static_cast<std::int64_t>(segment.begin_row), 1});
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

auto speed_segment_value(const speed_curve_segment_t& segment, signed_rational_t row,
                         signed_rational_t seconds, std::uint64_t divisions)
    -> std::optional<signed_rational_t> {
    if (segment.transition_duration.denominator == 0) {
        return std::nullopt;
    }
    if (segment.transition_duration.numerator == 0) {
        return normalized(segment.target_multiplier);
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
        return normalized(pause.duration);
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
    const auto query = timing_at(timing, *row, timing_boundary_side_t::event);
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

auto visual_draw_order(visual_event_kind_t kind) -> std::size_t {
    switch (kind) {
    case visual_event_kind_t::background:
        return 0;
    case visual_event_kind_t::poor:
        return 1;
    case visual_event_kind_t::layer:
        return 2;
    case visual_event_kind_t::video:
        return 3;
    case visual_event_kind_t::storyboard:
        return 4;
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

} // namespace

auto build_timing_index(const chart_document_t& chart, timing_build_options_t options)
    -> timing_build_result_t {
    timing_build_result_t result;
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
    const auto offset = normalized(chart.beat_zero_audio_offset_seconds);
    if (!offset) {
        result.diagnostics.push_back(error(timing_error_code_t::invalid_document,
                                           "beat-zero audio offset is not a valid rational"));
        return result;
    }

    std::set<std::uint64_t> boundary_set{0, measures.total_rows};
    if (boundary_set.size() > options.maximum_boundaries) {
        result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                           "timing boundary count exceeds the configured cap"));
        return result;
    }
    const auto insert_boundary = [&](std::uint64_t row) -> bool {
        boundary_set.insert(row);
        return boundary_set.size() <= options.maximum_boundaries;
    };
    std::vector<positioned_bpm_t> bpms;
    bpms.reserve(chart.bpms.size());
    for (std::size_t order = 0; order < chart.bpms.size(); ++order) {
        const auto& bpm = chart.bpms[order];
        const auto row = absolute_row(measures, bpm.position);
        if (!row) {
            result.diagnostics.push_back(error(timing_error_code_t::position_out_of_range,
                                               "BPM position is outside chart geometry", bpm.id));
            continue;
        }
        if (bpm.beats_per_minute.numerator <= 0 || bpm.beats_per_minute.denominator == 0) {
            result.diagnostics.push_back(
                error(timing_error_code_t::invalid_bpm, "BPM must be a positive rational", bpm.id));
            continue;
        }
        if (!insert_boundary(*row)) {
            result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                               "timing boundary count exceeds the configured cap"));
            return result;
        }
        bpms.push_back({*row, &bpm, order});
    }
    std::ranges::stable_sort(bpms, [](const auto& left, const auto& right) {
        return std::tuple{left.row,
                          order_key(left.event->provenance, left.insertion_order, left.event->id)} <
               std::tuple{right.row, order_key(right.event->provenance, right.insertion_order,
                                               right.event->id)};
    });
    if (bpms.empty() || bpms.front().row != 0) {
        result.diagnostics.push_back(error(timing_error_code_t::missing_initial_bpm,
                                           "timing requires a positive BPM at chart row zero"));
    }

    std::vector<positioned_pause_t> pauses;
    pauses.reserve(chart.pauses.size());
    for (std::size_t order = 0; order < chart.pauses.size(); ++order) {
        const auto& pause = chart.pauses[order];
        const auto row = absolute_row(measures, pause.position);
        if (!row) {
            result.diagnostics.push_back(error(timing_error_code_t::position_out_of_range,
                                               "pause position is outside chart geometry",
                                               pause.id));
            continue;
        }
        if (pause.duration.numerator < 0 || pause.duration.denominator == 0) {
            result.diagnostics.push_back(error(timing_error_code_t::invalid_pause,
                                               "pause duration must be a nonnegative rational",
                                               pause.id));
            continue;
        }
        if (!insert_boundary(*row)) {
            result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                               "timing boundary count exceeds the configured cap"));
            return result;
        }
        pauses.push_back({*row, &pause, order});
    }
    std::ranges::stable_sort(pauses, [](const auto& left, const auto& right) {
        return std::tuple{left.row,
                          order_key(left.event->provenance, left.insertion_order, left.event->id)} <
               std::tuple{right.row, order_key(right.event->provenance, right.insertion_order,
                                               right.event->id)};
    });

    std::map<std::uint64_t, std::pair<std::size_t, std::size_t>> warp_edges;
    for (const auto& warp : chart.warps) {
        const auto begin = absolute_row(measures, warp.span.begin);
        const auto end = absolute_row(measures, warp.span.end);
        if (!begin || !end || *end <= *begin) {
            result.diagnostics.push_back(error(timing_error_code_t::invalid_warp,
                                               "warp must be a positive in-chart musical interval",
                                               warp.id));
            continue;
        }
        if (!insert_boundary(*begin) || !insert_boundary(*end)) {
            result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                               "timing boundary count exceeds the configured cap"));
            return result;
        }
        ++warp_edges[*begin].first;
        ++warp_edges[*end].second;
    }

    if (boundary_set.size() > options.maximum_boundaries) {
        result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                           "timing boundary count exceeds the configured cap"));
    }
    if (has_errors(result.diagnostics)) {
        return result;
    }

    timing_index_t index{.divisions_per_standard_measure = chart.divisions_per_standard_measure,
                         .total_rows = measures.total_rows,
                         .source_fingerprint = timing_fingerprint(chart)};
    index.rows.reserve(boundary_set.size());
    index.segments.reserve(boundary_set.size() - 1);
    std::vector<std::uint64_t> boundaries(boundary_set.begin(), boundary_set.end());
    auto current_time = *offset;
    auto current_bpm = signed_rational_t{};
    auto bpm_cursor = std::size_t{};
    auto pause_cursor = std::size_t{};
    auto active_warps = std::size_t{};

    for (std::size_t boundary_index = 0; boundary_index < boundaries.size(); ++boundary_index) {
        const auto row = boundaries[boundary_index];
        if (const auto edge = warp_edges.find(row); edge != warp_edges.end()) {
            if (edge->second.second > active_warps) {
                result.diagnostics.push_back(error(timing_error_code_t::invalid_warp,
                                                   "warp edge accounting is inconsistent"));
                return result;
            }
            active_warps -= edge->second.second;
            active_warps += edge->second.first;
        }
        while (bpm_cursor < bpms.size() && bpms[bpm_cursor].row == row) {
            current_bpm = *normalized(bpms[bpm_cursor].event->beats_per_minute);
            ++bpm_cursor;
        }
        if (current_bpm.numerator <= 0) {
            result.diagnostics.push_back(error(timing_error_code_t::missing_initial_bpm,
                                               "no effective BPM exists at this timing boundary"));
            return result;
        }

        const auto row_pause_begin = pause_cursor;
        while (pause_cursor < pauses.size() && pauses[pause_cursor].row == row) {
            ++pause_cursor;
        }
        const auto row_pause_end = pause_cursor;
        const auto apply_pause = [&](const pause_event_t& pause, const char* description) -> bool {
            const auto duration = pause_duration(pause, current_bpm);
            if (!duration) {
                result.diagnostics.push_back(
                    error(timing_error_code_t::arithmetic_overflow,
                          std::string(description) + " conversion exceeds exact timing bounds",
                          pause.id));
                return false;
            }
            if (active_warps != 0 && duration->numerator != 0) {
                result.diagnostics.push_back(
                    warning(timing_error_code_t::invalid_warp,
                            std::string(description) +
                                " inside a warp is suppressed by the canonical timing map",
                            pause.id));
                return true;
            }
            const auto added = add_exact(current_time, *duration);
            if (!added) {
                result.diagnostics.push_back(
                    error(timing_error_code_t::arithmetic_overflow,
                          std::string(description) + " accumulation exceeds exact timing bounds",
                          pause.id));
                return false;
            }
            current_time = *added;
            return true;
        };

        const auto before_delays = current_time;
        for (auto index = row_pause_begin; index < row_pause_end; ++index) {
            if (pauses[index].event->kind == pause_kind_t::delay &&
                !apply_pause(*pauses[index].event, "delay")) {
                return result;
            }
        }
        const auto event_time = current_time;
        for (auto index = row_pause_begin; index < row_pause_end; ++index) {
            if (pauses[index].event->kind == pause_kind_t::stop &&
                !apply_pause(*pauses[index].event, "stop")) {
                return result;
            }
        }
        index.rows.push_back({.absolute_row = row,
                              .before_delays_seconds = before_delays,
                              .event_seconds = event_time,
                              .after_stops_seconds = current_time,
                              .effective_bpm = current_bpm,
                              .inside_warp = active_warps != 0});

        if (boundary_index + 1 == boundaries.size()) {
            continue;
        }
        const auto next_row = boundaries[boundary_index + 1];
        auto segment_end = current_time;
        if (active_warps == 0) {
            const auto duration =
                row_duration(next_row - row, chart.divisions_per_standard_measure, current_bpm);
            if (!duration) {
                result.diagnostics.push_back(
                    error(timing_error_code_t::arithmetic_overflow,
                          "BPM segment exceeds exact timing arithmetic bounds"));
                return result;
            }
            const auto added = add_exact(current_time, *duration);
            if (!added) {
                result.diagnostics.push_back(
                    error(timing_error_code_t::arithmetic_overflow,
                          "timing segment accumulation exceeds exact arithmetic bounds"));
                return result;
            }
            segment_end = *added;
        }
        index.segments.push_back({.begin_row = row,
                                  .end_row = next_row,
                                  .begin_seconds = current_time,
                                  .end_seconds = segment_end,
                                  .effective_bpm = current_bpm,
                                  .warped = active_warps != 0});
        current_time = segment_end;
    }

    result.index = std::move(index);
    return result;
}

auto timing_at(const timing_index_t& index, std::uint64_t absolute_row, timing_boundary_side_t side)
    -> timing_query_result_t {
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

auto timing_at(const timing_index_t& index, const measure_index_t& measures,
               local_position_t position, timing_boundary_side_t side) -> timing_query_result_t {
    const auto row = absolute_row(measures, position);
    if (!row) {
        return {.error = error(timing_error_code_t::position_out_of_range,
                               "timing query position is outside chart geometry")};
    }
    return timing_at(index, *row, side);
}

auto musical_position_at(const timing_index_t& index, signed_rational_t seconds,
                         timing_inverse_options_t options) -> timing_position_query_result_t {
    const auto query = normalized(seconds);
    if (!query) {
        return {.error = error(timing_error_code_t::invalid_document,
                               "inverse timing query requires valid rational seconds")};
    }
    if (index.divisions_per_standard_measure == 0 || index.rows.empty() ||
        index.segments.size() + 1 != index.rows.size() || index.rows.front().absolute_row != 0 ||
        index.rows.back().absolute_row != index.total_rows) {
        return {.error = error(timing_error_code_t::invalid_document,
                               "inverse timing query requires a complete timing index")};
    }
    const auto before_chart = compare_exact(*query, index.rows.front().before_delays_seconds);
    const auto after_chart = compare_exact(*query, index.rows.back().after_stops_seconds);
    if (!before_chart || !after_chart) {
        return {.error = error(timing_error_code_t::invalid_document,
                               "timing index contains an invalid rational boundary")};
    }
    if (*before_chart < 0 || *after_chart > 0) {
        return {.error = error(timing_error_code_t::position_out_of_range,
                               "audio time lies outside chart timing bounds")};
    }

    // A warp collapses every row in its half-open span to one audio time. Find
    // that plateau first so its endpoint choice is independent of neighboring
    // row pauses or segments.
    const auto first_segment =
        std::lower_bound(index.segments.begin(), index.segments.end(), *query,
                         [](const timing_segment_t& segment, signed_rational_t value) {
                             const auto comparison = compare_exact(segment.end_seconds, value);
                             return comparison && *comparison < 0;
                         });
    if (first_segment != index.segments.end()) {
        const auto first_index = static_cast<std::size_t>(first_segment - index.segments.begin());
        auto warp_entry = std::optional<std::uint64_t>{};
        auto warp_exit = std::optional<std::uint64_t>{};
        for (auto segment_index = first_index; segment_index < index.segments.size();
             ++segment_index) {
            const auto& segment = index.segments[segment_index];
            const auto begin_comparison = compare_exact(segment.begin_seconds, *query);
            if (!begin_comparison || *begin_comparison > 0) {
                break;
            }
            if (!segment.warped) {
                continue;
            }
            const auto end_comparison = compare_exact(segment.end_seconds, *query);
            if (!end_comparison) {
                return {.error = error(timing_error_code_t::invalid_document,
                                       "timing index contains an invalid segment time")};
            }
            if (*begin_comparison == 0 && *end_comparison == 0) {
                if (!warp_entry) {
                    warp_entry = segment.begin_row;
                }
                warp_exit = segment.end_row;
            }
        }
        if (warp_entry && warp_exit) {
            const auto selected =
                options.warp_side == timing_warp_inverse_side_t::entry ? *warp_entry : *warp_exit;
            if (selected > maximum_i64_magnitude) {
                return {.error = error(timing_error_code_t::arithmetic_overflow,
                                       "inverse musical row exceeds exact position bounds")};
            }
            const auto row = std::ranges::lower_bound(index.rows, selected, {},
                                                      &timing_row_state_t::absolute_row);
            if (row == index.rows.end() || row->absolute_row != selected) {
                return {.error = error(timing_error_code_t::invalid_document,
                                       "warp endpoint is missing from the timing index")};
            }
            return {.absolute_row = signed_rational_t{static_cast<std::int64_t>(selected), 1},
                    .inside_warp = row->inside_warp};
        }
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
        const auto& segment = index.segments[row_index - 1];
        if (segment.warped) {
            return {.error = error(timing_error_code_t::invalid_document,
                                   "audio time does not identify a row on a warp plateau")};
        }
        const auto delta_seconds = subtract_exact(*query, segment.begin_seconds);
        if (!delta_seconds || delta_seconds->numerator < 0 ||
            segment.effective_bpm.numerator <= 0 || segment.effective_bpm.denominator == 0) {
            return {.error = error(timing_error_code_t::arithmetic_overflow,
                                   "inverse timing segment exceeds exact arithmetic bounds")};
        }
        const auto row_delta =
            positive_fraction({static_cast<std::uint64_t>(delta_seconds->numerator),
                               index.divisions_per_standard_measure,
                               static_cast<std::uint64_t>(segment.effective_bpm.numerator)},
                              {delta_seconds->denominator, 240, segment.effective_bpm.denominator});
        if (!row_delta || segment.begin_row > maximum_i64_magnitude) {
            return {.error = error(timing_error_code_t::arithmetic_overflow,
                                   "inverse musical row exceeds exact arithmetic bounds")};
        }
        const auto absolute =
            add_exact({static_cast<std::int64_t>(segment.begin_row), 1}, *row_delta);
        if (!absolute) {
            return {.error = error(timing_error_code_t::arithmetic_overflow,
                                   "inverse musical row exceeds exact arithmetic bounds")};
        }
        return {.absolute_row = *absolute};
    }
    return {.error = error(timing_error_code_t::position_out_of_range,
                           "audio time has no musical position in the timing index")};
}

auto sample_frame_at(signed_rational_t seconds, std::uint32_t sample_rate,
                     sample_rounding_t rounding) -> sample_frame_result_t {
    const auto value = normalized(seconds);
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
    if (!product) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "sample-frame conversion overflows")};
    }
    const auto whole = *product / denominator;
    const auto remainder = *product % denominator;
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

    enum class source_kind_t : std::uint8_t { scroll, speed };
    struct positioned_event_t {
        std::uint64_t row{};
        std::size_t insertion_order{};
        object_id_t id;
        std::size_t source_order{};
        source_kind_t kind{source_kind_t::scroll};
        const scroll_event_t* scroll{};
        const speed_event_t* speed{};
    };
    std::vector<positioned_event_t> events;
    events.reserve(chart.scrolls.size() + chart.speeds.size());
    for (std::size_t order = 0; order < chart.scrolls.size(); ++order) {
        const auto& event = chart.scrolls[order];
        const auto row = absolute_row(measures, event.position);
        if (!row) {
            result.diagnostics.push_back(error(timing_error_code_t::position_out_of_range,
                                               "scroll event position is outside chart geometry",
                                               event.id));
            continue;
        }
        if (!normalized(event.multiplier)) {
            result.diagnostics.push_back(error(timing_error_code_t::invalid_scroll,
                                               "scroll multiplier must be a valid signed rational",
                                               event.id));
            continue;
        }
        const auto position_timing = timing_at(timing, *row);
        if (!position_timing) {
            auto diagnostic = *position_timing.error;
            diagnostic.object = event.id;
            result.diagnostics.push_back(std::move(diagnostic));
            continue;
        }
        if (position_timing.inside_warp) {
            result.diagnostics.push_back(warning(
                timing_error_code_t::invalid_warp,
                "scroll event inside a half-open warp is suppressed by the scroll/speed evaluator",
                event.id));
            continue;
        }
        events.push_back({.row = *row,
                          .insertion_order = order,
                          .id = event.id,
                          .source_order = event.provenance.source_order,
                          .kind = source_kind_t::scroll,
                          .scroll = &event});
    }
    for (std::size_t order = 0; order < chart.speeds.size(); ++order) {
        const auto& event = chart.speeds[order];
        const auto row = absolute_row(measures, event.position);
        if (!row) {
            result.diagnostics.push_back(error(timing_error_code_t::position_out_of_range,
                                               "speed event position is outside chart geometry",
                                               event.id));
            continue;
        }
        if (event.multiplier.numerator < 0 || event.multiplier.denominator == 0 ||
            event.transition_beats.denominator == 0) {
            result.diagnostics.push_back(
                error(timing_error_code_t::invalid_speed,
                      "speed multiplier and duration must be bounded valid rationals", event.id));
            continue;
        }
        const auto position_timing = timing_at(timing, *row);
        if (!position_timing) {
            auto diagnostic = *position_timing.error;
            diagnostic.object = event.id;
            result.diagnostics.push_back(std::move(diagnostic));
            continue;
        }
        if (position_timing.inside_warp) {
            result.diagnostics.push_back(warning(
                timing_error_code_t::invalid_warp,
                "speed event inside a half-open warp is suppressed by the scroll/speed evaluator",
                event.id));
            continue;
        }
        if (*row > maximum_i64_magnitude) {
            result.diagnostics.push_back(error(timing_error_code_t::arithmetic_overflow,
                                               "speed event row exceeds exact query bounds",
                                               event.id));
            continue;
        }
        events.push_back({.row = *row,
                          .insertion_order = order,
                          .id = event.id,
                          .source_order = event.provenance.source_order,
                          .kind = source_kind_t::speed,
                          .speed = &event});
    }
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
    for (const auto& positioned : events) {
        if (positioned.kind == source_kind_t::scroll) {
            index.scroll_segments.push_back(
                {.begin_row = positioned.row,
                 .end_row = measures.total_rows,
                 .multiplier = *normalized(positioned.scroll->multiplier),
                 .source_event = positioned.id,
                 .source_order = positioned.source_order});
            continue;
        }

        const auto query_time = timing_at(timing, positioned.row, timing_boundary_side_t::event);
        if (!query_time) {
            auto diagnostic = *query_time.error;
            diagnostic.object = positioned.id;
            result.diagnostics.push_back(std::move(diagnostic));
            return result;
        }
        auto initial_multiplier = signed_rational_t{1, 1};
        if (!index.speed_segments.empty()) {
            const auto current = speed_segment_value(
                index.speed_segments.back(), {static_cast<std::int64_t>(positioned.row), 1},
                *query_time.seconds, chart.divisions_per_standard_measure);
            if (!current) {
                result.diagnostics.push_back(
                    error(timing_error_code_t::arithmetic_overflow,
                          "speed transition retargeting exceeds exact bounds", positioned.id));
                return result;
            }
            initial_multiplier = *current;
        }
        const auto& source = *positioned.speed;
        const auto target = normalized({static_cast<std::int64_t>(source.multiplier.numerator),
                                        source.multiplier.denominator});
        if (!target) {
            result.diagnostics.push_back(error(timing_error_code_t::invalid_speed,
                                               "speed multiplier cannot be normalized",
                                               positioned.id));
            return result;
        }
        index.speed_segments.push_back({.begin_row = positioned.row,
                                        .end_row = measures.total_rows,
                                        .begin_seconds = *query_time.seconds,
                                        .initial_multiplier = initial_multiplier,
                                        .target_multiplier = *target,
                                        .transition_duration = source.transition_beats,
                                        .transition_is_seconds = source.transition_is_seconds,
                                        .source_event = positioned.id,
                                        .source_order = positioned.source_order});
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

    std::vector<signed_rational_t> displacement_boundaries;
    displacement_boundaries.push_back({0, 1});
    displacement_boundaries.push_back({static_cast<std::int64_t>(measures.total_rows), 1});
    for (const auto& row : timing.rows) {
        displacement_boundaries.push_back({static_cast<std::int64_t>(row.absolute_row), 1});
    }
    for (const auto& segment : index.scroll_segments) {
        displacement_boundaries.push_back({static_cast<std::int64_t>(segment.begin_row), 1});
    }
    for (const auto& segment : index.speed_segments) {
        displacement_boundaries.push_back({static_cast<std::int64_t>(segment.begin_row), 1});
        if (segment.transition_duration.numerator == 0) {
            continue;
        }
        if (!segment.transition_is_seconds) {
            const auto transition_rows = positive_fraction(
                {segment.transition_duration.numerator, chart.divisions_per_standard_measure},
                {segment.transition_duration.denominator, 4});
            const auto end_row =
                transition_rows
                    ? add_exact({static_cast<std::int64_t>(segment.begin_row), 1}, *transition_rows)
                    : std::nullopt;
            if (end_row) {
                const auto in_chart = compare_row_to_integer(*end_row, measures.total_rows);
                if (in_chart && *in_chart < 0) {
                    displacement_boundaries.push_back(*end_row);
                }
            }
            continue;
        }

        auto duration_numerator = segment.transition_duration.numerator;
        auto duration_denominator = segment.transition_duration.denominator;
        const auto duration_divisor = std::gcd(duration_numerator, duration_denominator);
        duration_numerator /= duration_divisor;
        duration_denominator /= duration_divisor;
        if (duration_numerator <= maximum_i64_magnitude) {
            const auto completion_time =
                add_exact(segment.begin_seconds,
                          {static_cast<std::int64_t>(duration_numerator), duration_denominator});
            if (completion_time) {
                const auto completion_position = musical_position_at(timing, *completion_time);
                if (completion_position && completion_position.absolute_row) {
                    const auto in_chart = compare_row_to_integer(*completion_position.absolute_row,
                                                                 measures.total_rows);
                    const auto after_start =
                        compare_exact(*completion_position.absolute_row,
                                      {static_cast<std::int64_t>(segment.begin_row), 1});
                    if (in_chart && *in_chart < 0 && after_start && *after_start > 0) {
                        displacement_boundaries.push_back(*completion_position.absolute_row);
                    }
                }
            }
        }
    }
    std::ranges::sort(displacement_boundaries, [](signed_rational_t left, signed_rational_t right) {
        const auto comparison = compare_exact(left, right);
        return comparison && *comparison < 0;
    });
    displacement_boundaries.erase(
        std::unique(displacement_boundaries.begin(), displacement_boundaries.end(),
                    [](signed_rational_t left, signed_rational_t right) {
                        const auto comparison = compare_exact(left, right);
                        return comparison && *comparison == 0;
                    }),
        displacement_boundaries.end());
    if (displacement_boundaries.size() < 2 ||
        displacement_boundaries.size() - 1 > options.maximum_segments) {
        result.diagnostics.push_back(
            error(timing_error_code_t::capacity_exceeded,
                  "scroll displacement segment count exceeds the configured cap"));
        return result;
    }

    const auto components_at = [&](signed_rational_t row, timing_boundary_side_t side)
        -> std::optional<std::pair<signed_rational_t, signed_rational_t>> {
        const auto query_time = timing_seconds_at_row(timing, row, side);
        if (!query_time) {
            result.diagnostics.push_back(*query_time.error);
            return std::nullopt;
        }
        const auto before_event_side = side == timing_boundary_side_t::before_delays;
        const auto find_segment = [&](const auto& segments) {
            return std::upper_bound(
                segments.begin(), segments.end(), row,
                [&](signed_rational_t query, const auto& segment) {
                    const auto comparison = compare_row_to_integer(query, segment.begin_row);
                    return comparison &&
                           (*comparison < 0 || (*comparison == 0 && before_event_side));
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
                result.diagnostics.push_back(
                    error(timing_error_code_t::arithmetic_overflow,
                          "scroll displacement speed evaluation exceeds exact bounds",
                          segment.source_event));
                return std::nullopt;
            }
            speed = *value;
        }
        return std::pair{scroll, speed};
    };

    auto cumulative_displacement = signed_rational_t{};
    index.displacement_segments.reserve(displacement_boundaries.size() - 1);
    for (auto boundary = std::size_t{}; boundary + 1 < displacement_boundaries.size(); ++boundary) {
        const auto begin_row = displacement_boundaries[boundary];
        const auto end_row = displacement_boundaries[boundary + 1];
        const auto begin_components = components_at(begin_row, timing_boundary_side_t::after_stops);
        const auto end_components = components_at(end_row, timing_boundary_side_t::before_delays);
        if (!begin_components || !end_components) {
            return result;
        }
        const auto row_width = subtract_exact(end_row, begin_row);
        const auto midpoint_delta = row_width ? multiply_exact(*row_width, {1, 2}) : std::nullopt;
        const auto midpoint = midpoint_delta ? add_exact(begin_row, *midpoint_delta) : std::nullopt;
        const auto midpoint_time =
            midpoint ? timing_seconds_at_row(timing, *midpoint, timing_boundary_side_t::event)
                     : timing_query_result_t{};
        if (!midpoint_time) {
            if (midpoint_time.error) {
                result.diagnostics.push_back(*midpoint_time.error);
            } else {
                result.diagnostics.push_back(
                    error(timing_error_code_t::arithmetic_overflow,
                          "scroll displacement midpoint exceeds exact bounds"));
            }
            return result;
        }
        const bool warped = midpoint_time.inside_warp;
        index.displacement_segments.push_back({.begin_row = begin_row,
                                               .end_row = end_row,
                                               .cumulative_begin_beats = cumulative_displacement,
                                               .scroll_multiplier = begin_components->first,
                                               .speed_begin_multiplier = begin_components->second,
                                               .warped = warped});
        if (warped) {
            continue;
        }

        const auto beat_width =
            positive_fraction({magnitude(row_width->numerator), 4},
                              {row_width->denominator, chart.divisions_per_standard_measure});
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
            result.diagnostics.push_back(
                error(timing_error_code_t::arithmetic_overflow,
                      "scroll displacement exceeds exact arithmetic bounds"));
            return result;
        }
        cumulative_displacement = *next_cumulative;
    }
    index.total_displacement_beats = cumulative_displacement;
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

auto scroll_speed_at(const scroll_speed_index_t& index, const timing_index_t& timing,
                     signed_rational_t absolute_row, timing_boundary_side_t side)
    -> scroll_speed_query_result_t {
    if (index.divisions_per_standard_measure != timing.divisions_per_standard_measure ||
        index.total_rows != timing.total_rows ||
        index.timing_source_fingerprint != timing.source_fingerprint) {
        return {.error = error(timing_error_code_t::invalid_document,
                               "scroll/speed index does not match the timing index")};
    }
    const auto row = normalized(absolute_row);
    if (!row) {
        return {.error = error(timing_error_code_t::invalid_document,
                               "scroll/speed query requires a valid rational row")};
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
    auto displacement = index.total_displacement_beats;
    const auto at_chart_end = compare_row_to_integer(*row, index.total_rows);
    if (!at_chart_end) {
        return {.error = error(timing_error_code_t::invalid_document,
                               "scroll/speed query row cannot be compared exactly")};
    }
    if (*at_chart_end < 0) {
        const auto after_displacement_segment = std::upper_bound(
            index.displacement_segments.begin(), index.displacement_segments.end(), *row,
            [](signed_rational_t query, const scroll_displacement_segment_t& segment) {
                const auto comparison = compare_exact(query, segment.begin_row);
                return comparison && *comparison < 0;
            });
        if (after_displacement_segment == index.displacement_segments.begin()) {
            return {.error = error(timing_error_code_t::invalid_document,
                                   "scroll displacement index has no containing segment")};
        }
        const auto& segment = *std::prev(after_displacement_segment);
        const auto begin_comparison = compare_exact(*row, segment.begin_row);
        const auto end_comparison = compare_exact(*row, segment.end_row);
        if (!begin_comparison || !end_comparison || *begin_comparison < 0 || *end_comparison > 0) {
            return {.error = error(timing_error_code_t::invalid_document,
                                   "scroll displacement segment does not contain the query row")};
        }
        displacement = segment.cumulative_begin_beats;
        if (!segment.warped && *begin_comparison > 0) {
            const auto row_distance = subtract_exact(*row, segment.begin_row);
            const auto beat_distance =
                row_distance && row_distance->numerator >= 0
                    ? positive_fraction(
                          {magnitude(row_distance->numerator), 4},
                          {row_distance->denominator, index.divisions_per_standard_measure})
                    : std::nullopt;
            const auto speed_sum = add_exact(segment.speed_begin_multiplier, speed_multiplier);
            const auto average_speed =
                speed_sum ? multiply_exact(*speed_sum, {1, 2}) : std::nullopt;
            const auto average_effective =
                average_speed ? multiply_exact(segment.scroll_multiplier, *average_speed)
                              : std::nullopt;
            const auto partial = average_effective && beat_distance
                                     ? multiply_exact(*average_effective, *beat_distance)
                                     : std::nullopt;
            const auto accumulated = partial ? add_exact(displacement, *partial) : std::nullopt;
            if (!accumulated) {
                return {.error =
                            error(timing_error_code_t::arithmetic_overflow,
                                  "scroll displacement query exceeds exact arithmetic bounds")};
            }
            displacement = *accumulated;
        }
    }
    const auto render_multiplier =
        static_cast<double>(effective->numerator) / static_cast<double>(effective->denominator);
    const auto render_displacement =
        static_cast<double>(displacement.numerator) / static_cast<double>(displacement.denominator);
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

auto scroll_speed_transition_span(const scroll_speed_index_t& index, const timing_index_t& timing,
                                  object_id_t source_event)
    -> scroll_speed_transition_span_result_t {
    const auto span_error = [&](timing_error_code_t code, std::string message) {
        return scroll_speed_transition_span_result_t{
            .error = error(code, std::move(message), source_event)};
    };
    if (index.divisions_per_standard_measure != timing.divisions_per_standard_measure ||
        index.total_rows != timing.total_rows ||
        index.timing_source_fingerprint != timing.source_fingerprint) {
        return span_error(timing_error_code_t::invalid_document,
                          "scroll/speed index does not match the timing index");
    }
    const auto segment =
        std::ranges::find(index.speed_segments, source_event, &speed_curve_segment_t::source_event);
    if (segment == index.speed_segments.end()) {
        return span_error(timing_error_code_t::position_out_of_range,
                          "SPEEDS object is not present in the compiled index");
    }
    if (segment->begin_row > segment->end_row || segment->end_row > index.total_rows ||
        segment->begin_row > maximum_i64_magnitude ||
        segment->transition_duration.denominator == 0) {
        return span_error(timing_error_code_t::invalid_document,
                          "compiled SPEEDS transition span is invalid");
    }

    const auto begin_row = signed_rational_t{static_cast<std::int64_t>(segment->begin_row), 1};
    const auto begin_seconds = segment->begin_seconds;
    auto end_row = begin_row;
    auto end_seconds = begin_seconds;
    auto clipped = false;
    const auto duration = segment->transition_duration;
    if (duration.numerator == 0) {
        return {.span = scroll_speed_transition_span_t{.source_event = source_event,
                                                       .begin_row = begin_row,
                                                       .end_row = end_row,
                                                       .begin_seconds = begin_seconds,
                                                       .end_seconds = end_seconds,
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

    if (segment->transition_is_seconds) {
        const auto available = subtract_exact(*cap_time.seconds, begin_seconds);
        if (!available || available->numerator < 0) {
            return span_error(timing_error_code_t::arithmetic_overflow,
                              "SPEEDS transition end exceeds exact timing bounds");
        }
        const auto duration_comparison =
            compare_positive_fractions(duration.numerator, duration.denominator,
                                       magnitude(available->numerator), available->denominator);
        if (duration_comparison >= 0) {
            clipped = duration_comparison > 0;
            end_row = cap_row;
            end_seconds = *cap_time.seconds;
        } else {
            auto duration_numerator = duration.numerator;
            auto duration_denominator = duration.denominator;
            const auto duration_divisor = std::gcd(duration_numerator, duration_denominator);
            duration_numerator /= duration_divisor;
            duration_denominator /= duration_divisor;
            if (duration_numerator > maximum_i64_magnitude) {
                return span_error(timing_error_code_t::arithmetic_overflow,
                                  "SPEEDS transition duration exceeds exact timing bounds");
            }
            const auto natural_end = add_exact(
                begin_seconds,
                {static_cast<std::int64_t>(duration_numerator), duration_denominator});
            if (!natural_end) {
                return span_error(timing_error_code_t::arithmetic_overflow,
                                  "SPEEDS transition end exceeds exact timing bounds");
            }
            const auto position = musical_position_at(timing, *natural_end);
            if (!position) {
                return span_error(position.error->code, position.error->message);
            }
            end_row = *position.absolute_row;
            end_seconds = *natural_end;
        }
    } else {
        const auto available_rows = subtract_exact(cap_row, begin_row);
        const auto available_beats =
            available_rows && available_rows->numerator >= 0
                ? positive_fraction(
                      {magnitude(available_rows->numerator), 4},
                      {available_rows->denominator, index.divisions_per_standard_measure})
                : std::nullopt;
        if (!available_rows || !available_beats) {
            return span_error(timing_error_code_t::arithmetic_overflow,
                              "SPEEDS transition span exceeds exact row bounds");
        }
        const auto duration_comparison = compare_positive_fractions(
            duration.numerator, duration.denominator, magnitude(available_beats->numerator),
            available_beats->denominator);
        if (duration_comparison > 0) {
            clipped = true;
            end_row = cap_row;
            if (replaced) {
                end_seconds = *cap_time.seconds;
            } else {
                const auto arrival =
                    timing_seconds_at_row(timing, cap_row, timing_boundary_side_t::before_delays);
                if (!arrival) {
                    return span_error(arrival.error->code, arrival.error->message);
                }
                end_seconds = *arrival.seconds;
            }
        } else {
            const auto transition_rows =
                positive_fraction({duration.numerator, index.divisions_per_standard_measure},
                                  {duration.denominator, 4});
            const auto natural_end =
                transition_rows ? add_exact(begin_row, *transition_rows) : std::nullopt;
            if (!natural_end) {
                return span_error(timing_error_code_t::arithmetic_overflow,
                                  "SPEEDS transition end exceeds exact row bounds");
            }
            end_row = *natural_end;
            const auto arrival =
                timing_seconds_at_row(timing, end_row, timing_boundary_side_t::before_delays);
            if (!arrival) {
                return span_error(arrival.error->code, arrival.error->message);
            }
            end_seconds = *arrival.seconds;
        }
    }

    return {.span = scroll_speed_transition_span_t{.source_event = source_event,
                                                   .begin_row = begin_row,
                                                   .end_row = end_row,
                                                   .begin_seconds = begin_seconds,
                                                   .end_seconds = end_seconds,
                                                   .transition_is_seconds =
                                                       segment->transition_is_seconds,
                                                   .clipped = clipped}};
}

auto build_preview_schedule(const chart_document_t& chart, const timing_index_t& timing,
                            preview_schedule_options_t options) -> preview_schedule_result_t {
    preview_schedule_result_t result;
    if (options.sample_rate == 0 ||
        timing.divisions_per_standard_measure != chart.divisions_per_standard_measure) {
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
    preview_schedule_t schedule{.sample_rate = options.sample_rate};
    auto playable_candidates = std::size_t{};
    auto bgm_candidates = std::size_t{};
    auto playable_overflow = false;
    auto bgm_overflow = false;
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (note.sound && (note.kind == note_kind_t::tap || note.kind == note_kind_t::hold ||
                               note.kind == note_kind_t::roll)) {
                if (playable_candidates < options.maximum_playable_events) {
                    ++playable_candidates;
                } else {
                    playable_overflow = true;
                }
            }
        }
        for (const auto& bgm : measure.bgm_events) {
            if (bgm.sound) {
                if (bgm_candidates < options.maximum_bgm_events) {
                    ++bgm_candidates;
                } else {
                    bgm_overflow = true;
                }
            }
        }
    }
    if (playable_overflow || bgm_overflow || chart.bga_events.size() > options.maximum_bga_events ||
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
    schedule.playable.reserve(playable_candidates);
    schedule.bgm.reserve(bgm_candidates);
    schedule.bga.reserve(std::min(chart.bga_events.size(), options.maximum_bga_events));
    std::unordered_map<std::uint64_t, const measure_index_entry_t*> measure_entries;
    measure_entries.reserve(measures.entries.size());
    for (const auto& entry : measures.entries) {
        measure_entries.emplace(entry.id.value, &entry);
    }
    const auto row_for = [&](local_position_t position) -> std::optional<std::uint64_t> {
        const auto entry = measure_entries.find(position.measure.value);
        if (entry == measure_entries.end() || position.row >= entry->second->row_capacity ||
            position.row >
                std::numeric_limits<std::uint64_t>::max() - entry->second->absolute_begin_row) {
            return std::nullopt;
        }
        return entry->second->absolute_begin_row + position.row;
    };

    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (!note.sound || (note.kind != note_kind_t::tap && note.kind != note_kind_t::hold &&
                                note.kind != note_kind_t::roll)) {
                continue;
            }
            const auto frame = position_frame(timing, row_for(note.position), options,
                                              result.diagnostics, note.id);
            if (frame) {
                if (schedule.playable.size() >= options.maximum_playable_events) {
                    result.diagnostics.push_back(
                        error(timing_error_code_t::capacity_exceeded,
                              "playable preview queue exceeds its configured cap", note.id));
                    return result;
                }
                schedule.playable.push_back({.sample_frame = *frame,
                                             .object = note.id,
                                             .resource = *note.sound,
                                             .lane = note.lane,
                                             .source_order = note.provenance.source_order});
            }
        }
        for (const auto& bgm : measure.bgm_events) {
            if (!bgm.sound) {
                continue;
            }
            const auto frame =
                position_frame(timing, row_for(bgm.position), options, result.diagnostics, bgm.id);
            if (!frame) {
                continue;
            }
            if (schedule.bgm.size() >= options.maximum_bgm_events) {
                result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                                   "BGM preview queue exceeds its configured cap",
                                                   bgm.id));
                return result;
            }
            const auto lane =
                std::ranges::find(chart.bgm_source_lanes, bgm.source_lane, &bgm_source_lane_t::id);
            schedule.bgm.push_back({.sample_frame = *frame,
                                    .object = bgm.id,
                                    .resource = *bgm.sound,
                                    .source_order = lane == chart.bgm_source_lanes.end()
                                                        ? std::numeric_limits<std::size_t>::max()
                                                        : lane->source_order,
                                    .secondary_order = bgm.provenance.source_order});
        }
    }
    for (const auto& bga : chart.bga_events) {
        const auto frame =
            position_frame(timing, row_for(bga.position), options, result.diagnostics, bga.id);
        if (frame) {
            if (schedule.bga.size() >= options.maximum_bga_events) {
                result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                                   "BGA preview queue exceeds its configured cap",
                                                   bga.id));
                return result;
            }
            schedule.bga.push_back({.sample_frame = *frame,
                                    .object = bga.id,
                                    .resource = bga.resource,
                                    .kind = bga.kind,
                                    .draw_order = visual_draw_order(bga.kind),
                                    .source_order = bga.provenance.source_order});
        }
    }

    if (options.include_metronome) {
        const auto pulse_count_for =
            [&](std::size_t measure_index) -> std::optional<std::uint64_t> {
            const auto& measure = chart.measures[measure_index];
            const auto& entry = measures.entries[measure_index];
            auto pulses = std::uint64_t{4};
            if (measure.definition.meter) {
                pulses = measure.definition.meter->numerator;
            } else {
                const auto quarter_rows = chart.divisions_per_standard_measure / 4;
                pulses = quarter_rows == 0
                             ? 1
                             : std::max<std::uint64_t>(1, entry.row_capacity / quarter_rows);
            }
            if (pulses == 0 || entry.row_capacity % pulses != 0) {
                return std::nullopt;
            }
            return pulses;
        };
        const auto accent_for = [](std::uint64_t pulse, bool compound) {
            return pulse == 0                   ? metronome_accent_t::measure
                   : compound && pulse % 3 == 0 ? metronome_accent_t::group
                                                : metronome_accent_t::regular;
        };

        if (options.count_in_pulses != 0) {
            const auto pulses = pulse_count_for(0);
            if (pulses) {
                const auto& first_measure = chart.measures.front();
                const auto step = measures.entries.front().row_capacity / *pulses;
                const auto row_zero = timing_at(timing, 0, timing_boundary_side_t::event);
                const auto initial_bpm = timing.rows.empty()
                                             ? std::optional<signed_rational_t>{}
                                             : normalized(timing.rows.front().effective_bpm);
                if (!row_zero || !initial_bpm || initial_bpm->numerator <= 0) {
                    result.diagnostics.push_back(row_zero.error.value_or(
                        error(timing_error_code_t::invalid_document,
                              "count-in requires a valid positive BPM at row zero")));
                    return result;
                }
                // One metronome pulse spans `step` chart rows. Extrapolate that
                // exact duration backward from row zero's event-side time.
                const auto pulse_seconds =
                    positive_fraction({step, 240, initial_bpm->denominator},
                                      {chart.divisions_per_standard_measure,
                                       static_cast<std::uint64_t>(initial_bpm->numerator)});
                if (!pulse_seconds) {
                    result.diagnostics.push_back(
                        error(timing_error_code_t::arithmetic_overflow,
                              "count-in pulse duration exceeds exact timing range"));
                    return result;
                }
                const bool compound =
                    first_measure.definition.meter && *pulses > 3 && *pulses % 3 == 0;
                bool reported_negative_count_in_time{};
                for (auto remaining = options.count_in_pulses; remaining > 0; --remaining) {
                    const auto elapsed =
                        multiply_exact(*pulse_seconds, {static_cast<std::int64_t>(remaining), 1});
                    const auto seconds =
                        elapsed ? subtract_exact(*row_zero.seconds, *elapsed) : std::nullopt;
                    if (!seconds) {
                        result.diagnostics.push_back(
                            error(timing_error_code_t::arithmetic_overflow,
                                  "count-in time exceeds exact timing range"));
                        return result;
                    }
                    const auto frame =
                        sample_frame_at(*seconds, options.sample_rate, options.rounding);
                    if (!frame) {
                        auto diagnostic = *frame.error;
                        if (diagnostic.code == timing_error_code_t::negative_sample_frame) {
                            if (!reported_negative_count_in_time) {
                                diagnostic.severity = diagnostic_severity_t::warning;
                                diagnostic.message =
                                    "one or more count-in pulses precede audio frame zero and "
                                    "were omitted";
                                result.diagnostics.push_back(std::move(diagnostic));
                                reported_negative_count_in_time = true;
                            }
                            continue;
                        }
                        result.diagnostics.push_back(std::move(diagnostic));
                        continue;
                    }
                    if (schedule.metronome.size() >= options.maximum_metronome_events) {
                        result.diagnostics.push_back(
                            error(timing_error_code_t::capacity_exceeded,
                                  "metronome preview queue exceeds its configured cap"));
                        return result;
                    }
                    const auto signed_pulse = -static_cast<std::int64_t>(remaining);
                    const auto remainder = magnitude(signed_pulse) % *pulses;
                    const auto pulse_in_measure = remainder == 0 ? 0 : *pulses - remainder;
                    schedule.metronome.push_back({
                        .sample_frame = *frame.frame,
                        .measure = first_measure.id,
                        .pulse = signed_pulse,
                        .accent = accent_for(pulse_in_measure, compound),
                    });
                }
            }
        }

        for (std::size_t measure_index = 0; measure_index < chart.measures.size();
             ++measure_index) {
            const auto& measure = chart.measures[measure_index];
            const auto& entry = measures.entries[measure_index];
            const auto pulses = pulse_count_for(measure_index);
            if (!pulses) {
                result.diagnostics.push_back(
                    error(timing_error_code_t::invalid_document,
                          "measure meter cannot produce exact metronome pulse rows"));
                continue;
            }
            const auto step = entry.row_capacity / *pulses;
            const bool compound = measure.definition.meter && *pulses > 3 && *pulses % 3 == 0;
            for (std::uint64_t pulse = 0; pulse < *pulses; ++pulse) {
                const auto row = entry.absolute_begin_row + pulse * step;
                const auto query = timing_at(timing, row, timing_boundary_side_t::event);
                if (!query) {
                    result.diagnostics.push_back(*query.error);
                    continue;
                }
                if (query.inside_warp) {
                    continue;
                }
                const auto frame =
                    sample_frame_at(*query.seconds, options.sample_rate, options.rounding);
                if (!frame) {
                    auto diagnostic = *frame.error;
                    if (diagnostic.code == timing_error_code_t::negative_sample_frame) {
                        diagnostic.severity = diagnostic_severity_t::warning;
                    }
                    result.diagnostics.push_back(std::move(diagnostic));
                    continue;
                }
                if (schedule.metronome.size() >= options.maximum_metronome_events) {
                    result.diagnostics.push_back(
                        error(timing_error_code_t::capacity_exceeded,
                              "metronome preview queue exceeds its configured cap"));
                    return result;
                }
                if (pulse > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                    result.diagnostics.push_back(
                        error(timing_error_code_t::arithmetic_overflow,
                              "metronome pulse index exceeds its signed representation"));
                    return result;
                }
                schedule.metronome.push_back({
                    .sample_frame = *frame.frame,
                    .measure = measure.id,
                    .pulse = static_cast<std::int64_t>(pulse),
                    .accent = accent_for(pulse, compound),
                });
            }
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
    result.schedule = std::move(schedule);
    return result;
}

} // namespace wormedit
