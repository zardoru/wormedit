#include <wormedit/audio/aligned_waveform.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace wormedit::audio {
namespace {

auto frame_at(signed_rational_t seconds, std::uint32_t sample_rate, std::uint64_t total_frames,
              sample_rounding_t rounding) -> std::optional<std::uint64_t> {
    if (seconds.denominator == 0) {
        return std::nullopt;
    }
    if (seconds.numerator <= 0) {
        return 0;
    }
    const auto frame = sample_frame_at(seconds, sample_rate, rounding);
    if (!frame) {
        return std::nullopt;
    }
    return std::min(*frame.frame, total_frames);
}

} // namespace

auto project_aligned_waveform(const waveform_t& waveform, const timing_index_t& timing,
                              aligned_waveform_request_t request) -> aligned_waveform_result_t {
    aligned_waveform_result_t result;
    if (!std::isfinite(request.viewport_scroll_pixels) ||
        !std::isfinite(request.viewport_height_pixels) ||
        !std::isfinite(request.pixels_per_standard_measure) ||
        request.viewport_scroll_pixels < 0.0 || request.viewport_height_pixels <= 0.0 ||
        request.pixels_per_standard_measure <= 0.0 || request.maximum_rows == 0) {
        return result;
    }
    if (timing.divisions_per_standard_measure == 0 || timing.total_rows == 0 ||
        timing.rows.empty()) {
        result.status = aligned_waveform_status_t::invalid_timing;
        return result;
    }
    if (waveform.sample_rate == 0 || waveform.total_frames == 0 ||
        waveform.total_frames >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        result.status = aligned_waveform_status_t::invalid_waveform;
        return result;
    }
    if (request.viewport_height_pixels > static_cast<double>(request.maximum_rows) ||
        request.viewport_height_pixels > static_cast<double>(maximum_aligned_waveform_rows)) {
        result.status = aligned_waveform_status_t::capacity_exceeded;
        return result;
    }
    const auto height = static_cast<std::size_t>(std::ceil(request.viewport_height_pixels));
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
    result.status = aligned_waveform_status_t::projected;
    if (first_row >= last_row) {
        return result;
    }
    const auto begin_time = timing_at(timing, static_cast<std::uint64_t>(first_row),
                                      timing_boundary_side_t::before_delays);
    const auto end_time = timing_at(timing, static_cast<std::uint64_t>(last_row),
                                    timing_boundary_side_t::after_stops);
    if (!begin_time || !end_time) {
        result.status = aligned_waveform_status_t::invalid_timing;
        return result;
    }
    const auto begin = frame_at(*begin_time.seconds, waveform.sample_rate, waveform.total_frames,
                                sample_rounding_t::floor);
    const auto end = frame_at(*end_time.seconds, waveform.sample_rate, waveform.total_frames,
                              sample_rounding_t::ceil);
    if (!begin || !end) {
        result.status = aligned_waveform_status_t::invalid_timing;
        return result;
    }
    if (*begin >= *end) {
        return result;
    }
    const auto projection = project_waveform(
        waveform, {.begin_frame = *begin,
                   .end_frame = *end,
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
    for (const auto& column : projection.columns) {
        const auto midpoint = column.begin_frame + (column.end_frame - column.begin_frame) / 2U;
        if (midpoint > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            continue;
        }
        const auto musical = musical_position_at(
            timing, {static_cast<std::int64_t>(midpoint), waveform.sample_rate});
        if (!musical || !musical.absolute_row || musical.absolute_row->denominator == 0) {
            continue;
        }
        const auto row = static_cast<long double>(musical.absolute_row->numerator) /
                         musical.absolute_row->denominator;
        const auto y = row / rows_per_pixel - request.viewport_scroll_pixels;
        if (!std::isfinite(y) || y < 0.0L || y >= request.viewport_height_pixels) {
            continue;
        }
        const auto pixel = static_cast<std::size_t>(y);
        if (pixel >= height) {
            continue;
        }
        occupied[pixel] = true;
        pixels[pixel].y_pixels = static_cast<double>(pixel) + 0.5;
        for (std::size_t channel = 0; channel < output_channel_count; ++channel) {
            pixels[pixel].absolute_peak[channel] =
                std::max(pixels[pixel].absolute_peak[channel], column.absolute_peak[channel]);
            pixels[pixel].rms[channel] = std::max(pixels[pixel].rms[channel], column.rms[channel]);
        }
    }
    result.rows.reserve(height);
    for (std::size_t pixel = 0; pixel < height; ++pixel) {
        if (occupied[pixel]) {
            result.rows.push_back(pixels[pixel]);
        }
    }
    return result;
}

auto project_waveform_overview(const waveform_t& waveform, const timing_index_t& timing,
                               aligned_waveform_request_t viewport, std::size_t maximum_rows)
    -> waveform_overview_result_t {
    waveform_overview_result_t result;
    if (!std::isfinite(viewport.viewport_scroll_pixels) ||
        !std::isfinite(viewport.viewport_height_pixels) ||
        !std::isfinite(viewport.pixels_per_standard_measure) ||
        viewport.viewport_scroll_pixels < 0.0 || viewport.viewport_height_pixels <= 0.0 ||
        viewport.pixels_per_standard_measure <= 0.0 || maximum_rows == 0) {
        return result;
    }
    if (timing.divisions_per_standard_measure == 0 || timing.total_rows == 0 ||
        timing.rows.empty()) {
        result.status = aligned_waveform_status_t::invalid_timing;
        return result;
    }
    if (waveform.sample_rate == 0 || waveform.total_frames == 0 ||
        waveform.total_frames >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        result.status = aligned_waveform_status_t::invalid_waveform;
        return result;
    }
    if (maximum_rows > maximum_waveform_overview_rows) {
        result.status = aligned_waveform_status_t::capacity_exceeded;
        return result;
    }
    const auto rows_per_pixel = static_cast<long double>(timing.divisions_per_standard_measure) /
                                viewport.pixels_per_standard_measure;
    const auto first_row = std::clamp(
        std::floor(static_cast<long double>(viewport.viewport_scroll_pixels) * rows_per_pixel),
        0.0L, static_cast<long double>(timing.total_rows));
    const auto last_row =
        std::clamp(std::ceil((static_cast<long double>(viewport.viewport_scroll_pixels) +
                              viewport.viewport_height_pixels) *
                             rows_per_pixel),
                   0.0L, static_cast<long double>(timing.total_rows));
    const auto begin_time = timing_at(timing, static_cast<std::uint64_t>(first_row),
                                      timing_boundary_side_t::before_delays);
    const auto end_time = timing_at(timing, static_cast<std::uint64_t>(last_row),
                                    timing_boundary_side_t::after_stops);
    if (!begin_time || !end_time) {
        result.status = aligned_waveform_status_t::invalid_timing;
        return result;
    }
    const auto begin = frame_at(*begin_time.seconds, waveform.sample_rate, waveform.total_frames,
                                sample_rounding_t::floor);
    const auto end = frame_at(*end_time.seconds, waveform.sample_rate, waveform.total_frames,
                              sample_rounding_t::ceil);
    if (!begin || !end) {
        result.status = aligned_waveform_status_t::invalid_timing;
        return result;
    }
    result.visible_begin_fraction = static_cast<double>(*begin) / waveform.total_frames;
    result.visible_end_fraction = static_cast<double>(*end) / waveform.total_frames;
    const auto beat_zero = timing_at(timing, 0, timing_boundary_side_t::before_delays);
    if (!beat_zero || !beat_zero.seconds || beat_zero.seconds->denominator == 0) {
        result.status = aligned_waveform_status_t::invalid_timing;
        return result;
    }
    const auto beat_zero_fraction = static_cast<long double>(beat_zero.seconds->numerator) *
                                    waveform.sample_rate / beat_zero.seconds->denominator /
                                    waveform.total_frames;
    if (std::isfinite(beat_zero_fraction) && beat_zero_fraction >= 0.0L &&
        beat_zero_fraction <= 1.0L) {
        result.beat_zero_fraction = static_cast<double>(beat_zero_fraction);
    }
    const auto projection = project_waveform(waveform, {.begin_frame = 0,
                                                        .end_frame = waveform.total_frames,
                                                        .pixel_width = maximum_rows,
                                                        .maximum_columns = maximum_rows});
    if (!projection) {
        result.status = projection.status == waveform_projection_status_t::capacity_exceeded
                            ? aligned_waveform_status_t::capacity_exceeded
                            : aligned_waveform_status_t::invalid_waveform;
        return result;
    }
    result.rows.reserve(projection.columns.size());
    for (const auto& column : projection.columns) {
        const auto midpoint = column.begin_frame + (column.end_frame - column.begin_frame) / 2U;
        result.rows.push_back({static_cast<double>(midpoint) / waveform.total_frames,
                               std::max(column.absolute_peak[0], column.absolute_peak[1])});
    }
    result.status = aligned_waveform_status_t::projected;
    return result;
}

} // namespace wormedit::audio
