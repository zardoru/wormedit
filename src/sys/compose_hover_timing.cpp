#include "sys/compose_hover_timing.h"

#include <cstdio>
#include <limits>
#include <numeric>
#include <optional>
#include <utility>

namespace wormedit::sys {
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

auto tail_row_at_seconds(const timing_index_t& timing, signed_rational_t seconds,
                         std::uint64_t divisions_per_standard_measure)
    -> std::optional<signed_rational_t> {
    if (timing.rows.empty() || seconds.denominator == 0 || divisions_per_standard_measure == 0)
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

    const auto& bpm = timing.rows.back().effective_bpm;
    if (bpm.numerator <= 0 || bpm.denominator == 0) return std::nullopt;
    wide_unsigned_t numerator_factors[]{delta_numerator,
        static_cast<wide_unsigned_t>(bpm.numerator), divisions_per_standard_measure};
    wide_unsigned_t denominator_factors[]{*delta_denominator, bpm.denominator, 240};
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

auto frame_time_label(wide_signed_t time_numerator, wide_unsigned_t time_denominator,
                      char* output,
                      std::size_t output_size) -> bool {
    if (!output || output_size == 0 || time_denominator == 0) return false;
    const bool negative = time_numerator < 0;
    const auto magnitude = wide_magnitude(time_numerator);
    const auto whole_seconds = magnitude / time_denominator;
    const auto remaining = magnitude % time_denominator;
    const auto maximum_milliseconds = std::numeric_limits<std::uint64_t>::max();
    if (whole_seconds > maximum_milliseconds / 1000U) return false;
    const auto fractional_milliseconds =
        (remaining * 1000U + time_denominator / 2U) / time_denominator;
    const auto milliseconds_wide = whole_seconds * 1000U + fractional_milliseconds;
    if (milliseconds_wide > maximum_milliseconds) return false;
    const auto milliseconds = static_cast<std::uint64_t>(milliseconds_wide);
    const auto hours = milliseconds / 3'600'000U;
    const auto minutes = (milliseconds / 60'000U) % 60U;
    const auto seconds = (milliseconds / 1000U) % 60U;
    const auto millis = milliseconds % 1000U;
    const auto written = hours != 0
        ? std::snprintf(output, output_size, "%s%llu:%02llu:%02llu.%03llu",
              negative ? "-" : "", static_cast<unsigned long long>(hours),
              static_cast<unsigned long long>(minutes), static_cast<unsigned long long>(seconds),
              static_cast<unsigned long long>(millis))
        : std::snprintf(output, output_size, "%s%llu:%02llu.%03llu",
              negative ? "-" : "", static_cast<unsigned long long>(milliseconds / 60'000U),
              static_cast<unsigned long long>(seconds), static_cast<unsigned long long>(millis));
    return written > 0 && static_cast<std::size_t>(written) < output_size;
}

auto beat_at_row(const chart_document_t& chart, const measure_index_t& measures,
                 signed_rational_t row) -> std::pair<std::uint64_t, std::int64_t> {
    if (row.denominator == 0 || chart.divisions_per_standard_measure == 0)
        return {1, 1};
    if (row.numerator >= 0 &&
        static_cast<wide_unsigned_t>(row.numerator) >=
            static_cast<wide_unsigned_t>(measures.total_rows) * row.denominator) {
        const auto tail_numerator = static_cast<wide_unsigned_t>(row.numerator) -
            static_cast<wide_unsigned_t>(measures.total_rows) * row.denominator;
        const auto tail_measure_rows = static_cast<wide_unsigned_t>(
            chart.divisions_per_standard_measure) * row.denominator;
        if (tail_measure_rows == 0) return {measures.entries.size() + 1, 1};
        const auto tail_measure = tail_numerator / tail_measure_rows;
        const auto measure_begin = static_cast<wide_unsigned_t>(measures.total_rows) +
            tail_measure * chart.divisions_per_standard_measure;
        const auto local_numerator = static_cast<wide_signed_t>(row.numerator) -
            static_cast<wide_signed_t>(measure_begin * row.denominator);
        const auto beat_index = local_numerator * 4 /
            (static_cast<wide_signed_t>(chart.divisions_per_standard_measure) * row.denominator);
        return {static_cast<std::uint64_t>(measures.entries.size()) +
                    static_cast<std::uint64_t>(tail_measure) + 1,
                static_cast<std::int64_t>(beat_index + 1)};
    }
    std::size_t measure_index = 0;
    while (measure_index + 1 < measures.entries.size() &&
           row.numerator >= 0 &&
           static_cast<unsigned __int128>(row.numerator) >=
               static_cast<unsigned __int128>(measures.entries[measure_index + 1].absolute_begin_row) *
                   row.denominator) {
        ++measure_index;
    }
    const auto& measure = chart.measures[measure_index];
    const auto meter = measure.definition.meter.value_or(meter_t{4, 4});
    if (meter.numerator == 0 || meter.denominator == 0 || chart.divisions_per_standard_measure == 0)
        return {measure_index + 1, 1};
    const auto begin = measures.entries[measure_index].absolute_begin_row;
    const auto numerator = row.numerator - static_cast<__int128>(begin) * row.denominator;
    const auto scaled = numerator * meter.denominator;
    const auto beat_units = static_cast<__int128>(chart.divisions_per_standard_measure) *
                            row.denominator;
    auto beat_index = scaled / beat_units;
    if (scaled < 0 && scaled % beat_units != 0) --beat_index;
    return {measure_index + 1, static_cast<std::int64_t>(beat_index + 1)};
}

} // namespace

auto format_compose_hover_timing(const chart_document_t& chart,
                                 const measure_index_t& measures,
                                 const timing_index_t& timing,
                                 std::int64_t source_frame,
                                 std::uint32_t sample_rate, char* output,
                                 std::size_t output_size) -> bool {
    if (!output || output_size == 0 || sample_rate == 0 ||
        chart.beat_zero_audio_offset_seconds.denominator == 0 || measures.entries.empty() ||
        chart.measures.size() != measures.entries.size()) return false;
    output[0] = '\0';
    const auto seconds = signed_rational_t{source_frame, sample_rate};
    const auto position = musical_position_at(
        timing, seconds, {.extrapolate_before_chart = true});
    auto absolute_row = position.absolute_row;
    if (!absolute_row)
        absolute_row = tail_row_at_seconds(timing, seconds, chart.divisions_per_standard_measure);
    if (!absolute_row) return false;
    char time[32]{};
    const auto offset = chart.beat_zero_audio_offset_seconds;
    wide_signed_t display_time_numerator{};
    const auto source_scaled = static_cast<wide_signed_t>(source_frame) * offset.denominator;
    const auto offset_scaled = static_cast<wide_signed_t>(offset.numerator) * sample_rate;
    if (__builtin_sub_overflow(source_scaled, offset_scaled, &display_time_numerator))
        return false;
    const auto display_time_denominator =
        static_cast<wide_unsigned_t>(sample_rate) * offset.denominator;
    if (!frame_time_label(display_time_numerator, display_time_denominator,
                          time, sizeof(time))) return false;
    const auto [measure, beat] = beat_at_row(chart, measures, *absolute_row);
    const auto written = std::snprintf(output, output_size,
        "Measure %llu · beat %lld · %s",
        static_cast<unsigned long long>(measure), static_cast<long long>(beat), time);
    return written > 0 && static_cast<std::size_t>(written) < output_size;
}

} // namespace wormedit::sys
