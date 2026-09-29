#include <wormedit/audio/channel_waveform.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>

namespace wormedit::audio {
namespace {

auto magnitude(std::int64_t value) -> std::uint64_t {
    return value >= 0 ? static_cast<std::uint64_t>(value)
                      : static_cast<std::uint64_t>(-(value + 1)) + 1;
}

auto normalized(signed_rational_t value) -> std::optional<signed_rational_t> {
    if (value.denominator == 0)
        return std::nullopt;
    const auto divisor = std::gcd(magnitude(value.numerator), value.denominator);
    const auto reduced = magnitude(value.numerator) / divisor;
    if (value.numerator < 0 &&
        reduced == static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1U) {
        value.numerator = std::numeric_limits<std::int64_t>::min();
    } else {
        value.numerator = value.numerator < 0 ? -static_cast<std::int64_t>(reduced)
                                              : static_cast<std::int64_t>(reduced);
    }
    value.denominator /= divisor;
    return value;
}

auto checked_signed_multiply(std::int64_t value, std::uint64_t factor)
    -> std::optional<std::int64_t> {
    constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const auto limit = value < 0 ? maximum + 1U : maximum;
    const auto absolute = magnitude(value);
    if (absolute != 0 && factor > limit / absolute)
        return std::nullopt;
    const auto product = absolute * factor;
    if (value < 0 && product == maximum + 1U)
        return std::numeric_limits<std::int64_t>::min();
    return value < 0 ? -static_cast<std::int64_t>(product) : static_cast<std::int64_t>(product);
}

auto add_exact(signed_rational_t left, signed_rational_t right)
    -> std::optional<signed_rational_t> {
    const auto lhs = normalized(left);
    const auto rhs = normalized(right);
    if (!lhs || !rhs)
        return std::nullopt;
    const auto common = std::gcd(lhs->denominator, rhs->denominator);
    const auto left_scale = rhs->denominator / common;
    const auto right_scale = lhs->denominator / common;
    if (lhs->denominator > std::numeric_limits<std::uint64_t>::max() / left_scale)
        return std::nullopt;
    const auto numerator_left = checked_signed_multiply(lhs->numerator, left_scale);
    const auto numerator_right = checked_signed_multiply(rhs->numerator, right_scale);
    if (!numerator_left || !numerator_right)
        return std::nullopt;
    if ((*numerator_right > 0 &&
         *numerator_left > std::numeric_limits<std::int64_t>::max() - *numerator_right) ||
        (*numerator_right < 0 &&
         *numerator_left < std::numeric_limits<std::int64_t>::min() - *numerator_right))
        return std::nullopt;
    return normalized({*numerator_left + *numerator_right, lhs->denominator * left_scale});
}

auto subtract_exact(signed_rational_t left, signed_rational_t right)
    -> std::optional<signed_rational_t> {
    const auto lhs = normalized(left);
    const auto rhs = normalized(right);
    if (!lhs || !rhs)
        return std::nullopt;
    const auto common = std::gcd(lhs->denominator, rhs->denominator);
    const auto left_scale = rhs->denominator / common;
    const auto right_scale = lhs->denominator / common;
    if (lhs->denominator > std::numeric_limits<std::uint64_t>::max() / left_scale)
        return std::nullopt;
    const auto numerator_left = checked_signed_multiply(lhs->numerator, left_scale);
    const auto numerator_right = checked_signed_multiply(rhs->numerator, right_scale);
    if (!numerator_left || !numerator_right)
        return std::nullopt;
    if ((*numerator_right > 0 &&
         *numerator_left < std::numeric_limits<std::int64_t>::min() + *numerator_right) ||
        (*numerator_right < 0 &&
         *numerator_left > std::numeric_limits<std::int64_t>::max() + *numerator_right))
        return std::nullopt;
    return normalized({*numerator_left - *numerator_right, lhs->denominator * left_scale});
}

auto source_frame_at(signed_rational_t time, signed_rational_t origin, const waveform_t& waveform,
                     std::uint64_t source_begin, sample_rounding_t rounding)
    -> std::optional<std::uint64_t> {
    const auto elapsed = subtract_exact(time, origin);
    if (!elapsed)
        return std::nullopt;
    if (elapsed->numerator <= 0)
        return source_begin;
    const auto relative = sample_frame_at(*elapsed, waveform.sample_rate, rounding);
    if (!relative || *relative.frame > waveform.total_frames - source_begin)
        return waveform.total_frames;
    return source_begin + *relative.frame;
}

auto output_frame(std::uint64_t native_frame, std::uint32_t native_rate, const waveform_t& waveform)
    -> std::optional<std::uint64_t> {
    if (native_rate == 0 ||
        native_frame > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    const auto converted =
        sample_frame_at({static_cast<std::int64_t>(native_frame), native_rate},
                        waveform.sample_rate, sample_rounding_t::nearest_ties_to_even);
    return converted ? std::optional{std::min(*converted.frame, waveform.total_frames)}
                     : std::nullopt;
}

struct channel_row_context_t {
    const waveform_t& waveform;
    const timing_index_t& timing;
    const aligned_waveform_request_t& request;
    signed_rational_t origin;
    std::uint64_t source_begin;
    long double rows_per_pixel;
    std::size_t height;
};

auto channel_pixel_for_column(const channel_row_context_t& context,
                              const waveform_column_t& column)
    -> std::optional<std::size_t> {
    const auto midpoint = column.begin_frame + (column.end_frame - column.begin_frame) / 2U;
    const auto elapsed_frame = midpoint - context.source_begin;
    if (elapsed_frame > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    const auto time = add_exact(
        context.origin,
        {static_cast<std::int64_t>(elapsed_frame), context.waveform.sample_rate});
    if (!time) {
        return std::nullopt;
    }
    const auto musical = musical_position_at(context.timing, *time);
    if (!musical || !musical.absolute_row || musical.absolute_row->denominator == 0) {
        return std::nullopt;
    }
    const auto row = static_cast<long double>(musical.absolute_row->numerator) /
                     musical.absolute_row->denominator;
    const auto y = row / context.rows_per_pixel - context.request.viewport_scroll_pixels;
    if (!std::isfinite(y) || y < 0.0L || y >= context.request.viewport_height_pixels) {
        return std::nullopt;
    }
    const auto pixel = static_cast<std::size_t>(y);
    return pixel < context.height ? std::optional{pixel} : std::nullopt;
}

auto rasterize_channel_columns(const channel_row_context_t& context,
                               const std::vector<waveform_column_t>& columns,
                               std::vector<aligned_waveform_row_t>& pixels,
                               std::vector<bool>& occupied) -> void {
    for (const auto& column : columns) {
        const auto pixel = channel_pixel_for_column(context, column);
        if (!pixel.has_value()) {
            continue;
        }
        occupied[*pixel] = true;
        auto& row = pixels[*pixel];
        row.y_pixels = static_cast<double>(*pixel) + 0.5;
        for (std::size_t channel_index = 0; channel_index < output_channel_count; ++channel_index) {
            row.absolute_peak[channel_index] =
                std::max(row.absolute_peak[channel_index], column.absolute_peak[channel_index]);
            row.rms[channel_index] = std::max(row.rms[channel_index], column.rms[channel_index]);
        }
    }
}

auto valid_channel_waveform_request(const aligned_waveform_request_t& request) -> bool {
    return std::isfinite(request.viewport_scroll_pixels) &&
           std::isfinite(request.viewport_height_pixels) &&
           std::isfinite(request.pixels_per_standard_measure) &&
           request.viewport_height_pixels > 0.0 &&
           request.pixels_per_standard_measure > 0.0 && request.maximum_rows != 0;
}

auto valid_channel_source(const waveform_t& waveform, const sound_channel_t& channel,
                          channel_waveform_source_geometry_t source) -> bool {
    return source.sample_rate != 0 && waveform.sample_rate != 0 &&
           waveform.total_frames != 0 &&
           waveform.total_frames <=
               static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) &&
           (!source.total_frames || channel.source_clip.begin_frame < *source.total_frames);
}

auto channel_window_within_capacity(const aligned_waveform_request_t& request) -> bool {
    return request.viewport_height_pixels <= static_cast<double>(request.maximum_rows) &&
           request.viewport_height_pixels <=
               static_cast<double>(maximum_aligned_waveform_rows);
}

} // namespace

auto project_channel_waveform(const waveform_t& waveform, const chart_document_t& chart,
                              const sound_channel_t& channel, const timing_index_t& timing,
                              aligned_waveform_request_t request,
                              channel_waveform_source_geometry_t source)
    -> aligned_waveform_result_t {
    const auto slices =
        derive_sound_channel_slices(chart, channel, source.sample_rate, source.total_frames);
    return project_channel_waveform(waveform, channel, timing, request, slices, source);
}

auto project_channel_waveform(const waveform_t& waveform, const sound_channel_t& channel,
                              const timing_index_t& timing, aligned_waveform_request_t request,
                              const sound_slice_result_t& slices,
                              channel_waveform_source_geometry_t source)
    -> aligned_waveform_result_t {
    aligned_waveform_result_t result;
    if (!valid_channel_waveform_request(request))
        return result;
    if (timing.divisions_per_standard_measure == 0 || timing.total_rows == 0 ||
        timing.rows.empty()) {
        result.status = aligned_waveform_status_t::invalid_timing;
        return result;
    }
    if (!valid_channel_source(waveform, channel, source)) {
        result.status = aligned_waveform_status_t::invalid_waveform;
        return result;
    }
    if (!channel_window_within_capacity(request)) {
        result.status = aligned_waveform_status_t::capacity_exceeded;
        return result;
    }
    if (!slices) {
        result.status = aligned_waveform_status_t::invalid_timing;
        return result;
    }
    result.status = aligned_waveform_status_t::projected;
    if (slices.slices.empty())
        return result;
    const auto source_begin =
        output_frame(channel.source_clip.begin_frame, source.sample_rate, waveform);
    const auto first_slice_frame =
        output_frame(slices.slices.front().source_begin_frame, source.sample_rate, waveform);
    const auto source_end =
        slices.slices.back().source_end_frame
            ? output_frame(*slices.slices.back().source_end_frame, source.sample_rate, waveform)
            : std::optional{waveform.total_frames};
    if (!source_begin || !first_slice_frame || !source_end ||
        *source_begin >= waveform.total_frames) {
        result.status = aligned_waveform_status_t::invalid_waveform;
        return result;
    }
    const auto origin = timing_at(timing, slices.slices.front().absolute_row);
    if (!origin || !origin.seconds) {
        result.status = aligned_waveform_status_t::invalid_timing;
        return result;
    }
    const auto rows_per_pixel = static_cast<long double>(timing.divisions_per_standard_measure) /
                                request.pixels_per_standard_measure;
    const auto first_row = std::clamp(
        std::floor(static_cast<long double>(request.viewport_scroll_pixels) * rows_per_pixel), 0.0L,
        static_cast<long double>(timing.total_rows));
    const auto last_row =
        std::clamp(std::ceil((static_cast<long double>(request.viewport_scroll_pixels) +
                              request.viewport_height_pixels) *
                             rows_per_pixel),
                   0.0L, static_cast<long double>(timing.total_rows));
    if (first_row >= last_row || last_row < slices.slices.front().absolute_row)
        return result;
    const auto begin_time = timing_at(timing, static_cast<std::uint64_t>(first_row),
                                      timing_boundary_side_t::before_delays);
    const auto end_time = timing_at(timing, static_cast<std::uint64_t>(last_row),
                                    timing_boundary_side_t::after_stops);
    if (!begin_time || !end_time) {
        result.status = aligned_waveform_status_t::invalid_timing;
        return result;
    }
    const auto first_frame = source_frame_at(*begin_time.seconds, *origin.seconds, waveform,
                                             *source_begin, sample_rounding_t::floor);
    const auto last_frame = source_frame_at(*end_time.seconds, *origin.seconds, waveform,
                                            *source_begin, sample_rounding_t::ceil);
    if (!first_frame || !last_frame) {
        result.status = aligned_waveform_status_t::invalid_timing;
        return result;
    }
    const auto begin = std::max(*first_frame, *first_slice_frame);
    const auto end = std::min(*last_frame, *source_end);
    if (begin >= end)
        return result;
    const auto height = static_cast<std::size_t>(std::ceil(request.viewport_height_pixels));
    const auto projection = project_waveform(
        waveform, {.begin_frame = begin,
                   .end_frame = end,
                   .pixel_width = std::min(height * 2U, maximum_aligned_waveform_rows),
                   .maximum_columns = maximum_aligned_waveform_rows});
    if (!projection) {
        result.status = projection.status == waveform_projection_status_t::capacity_exceeded
                            ? aligned_waveform_status_t::capacity_exceeded
                            : aligned_waveform_status_t::invalid_waveform;
        return result;
    }
    std::vector<aligned_waveform_row_t> pixels(height);
    std::vector<bool> occupied(height);
    const channel_row_context_t row_context{waveform, timing, request, *origin.seconds,
                                            *source_begin, rows_per_pixel, height};
    rasterize_channel_columns(row_context, projection.columns, pixels, occupied);
    result.rows.reserve(height);
    for (std::size_t pixel = 0; pixel < height; ++pixel) {
        if (occupied[pixel])
            result.rows.push_back(pixels[pixel]);
    }
    return result;
}

} // namespace wormedit::audio
