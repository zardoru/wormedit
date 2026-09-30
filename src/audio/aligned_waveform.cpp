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

auto frame_at_fractional_row(const waveform_t& waveform, const timing_index_t& timing,
                             long double row, bool end) -> std::optional<long double> {
    const auto floor_row = static_cast<std::uint64_t>(std::floor(row));
    const auto fraction = row - floor_row;
    const auto before = timing_at(timing, floor_row,
        fraction == 0.0L && !end ? timing_boundary_side_t::before_delays
                                : timing_boundary_side_t::after_stops);
    if (!before || !before.seconds) return std::nullopt;
    auto seconds = static_cast<long double>(before.seconds->numerator) / before.seconds->denominator;
    if (fraction != 0.0L) {
        const auto after = timing_at(timing, floor_row + 1U, timing_boundary_side_t::before_delays);
        if (!after || !after.seconds) return std::nullopt;
        const auto next = static_cast<long double>(after.seconds->numerator) / after.seconds->denominator;
        seconds += (next - seconds) * fraction;
    }
    const auto frame = seconds * waveform.sample_rate;
    if (!std::isfinite(frame)) return std::nullopt;
    return end ? std::ceil(frame) : std::floor(frame);
}

struct aligned_row_context_t {
    const waveform_t& waveform;
    const timing_index_t& timing;
    const aligned_waveform_request_t& request;
    long double rows_per_pixel;
    std::size_t height;
};

auto pixel_for_column(const aligned_row_context_t& context, const waveform_column_t& column)
    -> std::optional<std::size_t> {
    const auto midpoint = column.begin_frame + (column.end_frame - column.begin_frame) / 2U;
    if (midpoint > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    const auto musical = musical_position_at(
        context.timing, {static_cast<std::int64_t>(midpoint), context.waveform.sample_rate});
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

auto accumulate_column(aligned_waveform_row_t& row, const waveform_column_t& column,
                       std::size_t pixel) -> void {
    row.y_pixels = static_cast<double>(pixel) + 0.5;
    for (std::size_t channel = 0; channel < output_channel_count; ++channel) {
        row.absolute_peak[channel] =
            std::max(row.absolute_peak[channel], column.absolute_peak[channel]);
        row.rms[channel] = std::max(row.rms[channel], column.rms[channel]);
    }
}

auto waveform_covers(const waveform_t& waveform, std::uint64_t begin, std::uint64_t end) -> bool {
    const auto finish = waveform.key.end_frame == 0 ? waveform.total_frames : waveform.key.end_frame;
    return waveform.key.frames_per_bin != 0 && begin >= waveform.key.begin_frame && end <= finish;
}

auto bin_span(const waveform_t& waveform, std::uint64_t begin, std::uint64_t end)
    -> std::optional<waveform_column_t> {
    if (!waveform_covers(waveform, begin, end) || begin >= end) return std::nullopt;
    const auto first = (begin - waveform.key.begin_frame) / waveform.key.frames_per_bin;
    const auto last = (end - 1U - waveform.key.begin_frame) / waveform.key.frames_per_bin;
    if (last >= waveform.bins.size()) return std::nullopt;
    waveform_column_t column{.begin_frame = begin, .end_frame = end};
    column.minimum = waveform.bins[first].minimum;
    column.maximum = waveform.bins[first].maximum;
    for (auto index = first; index <= last; ++index) {
        const auto& bin = waveform.bins[index];
        for (std::size_t channel = 0; channel < output_channel_count; ++channel) {
            column.minimum[channel] = std::min(column.minimum[channel], bin.minimum[channel]);
            column.maximum[channel] = std::max(column.maximum[channel], bin.maximum[channel]);
            column.absolute_peak[channel] = std::max(column.absolute_peak[channel], bin.absolute_peak[channel]);
            column.rms[channel] = std::max(column.rms[channel], bin.rms[channel]);
        }
    }
    return column;
}

void accumulate_pcm_column(waveform_column_t& column, const decoded_audio_block_t& block,
                           std::uint64_t begin, std::uint64_t end) {
    const auto block_begin = block.key.block_index * maximum_read_frames;
    for (auto frame = begin; frame < end; ++frame) {
        for (std::size_t channel = 0; channel < output_channel_count; ++channel) {
            const auto value = block.interleaved_stereo[(frame - block_begin) * output_channel_count + channel];
            column.minimum[channel] = std::min(column.minimum[channel], value);
            column.maximum[channel] = std::max(column.maximum[channel], value);
            column.absolute_peak[channel] = std::max(column.absolute_peak[channel], std::abs(value));
            column.rms[channel] += value * value;
        }
    }
}

auto pcm_span(decoded_block_cache_t& cache, const audio_source_identity_t& source,
              std::uint64_t begin, std::uint64_t end) -> std::optional<waveform_column_t> {
    waveform_column_t column{.begin_frame = begin, .end_frame = end,
                             .minimum = {1, 1}, .maximum = {-1, -1}};
    for (auto frame = begin; frame < end;) {
        const auto index = frame / maximum_read_frames;
        const auto next = std::min(end, (index + 1U) * maximum_read_frames);
        bool complete{};
        const bool found = cache.with_block({source, index}, [&](const auto& block) {
            complete = next - index * maximum_read_frames <= block.frame_count;
            if (complete) accumulate_pcm_column(column, block, frame, next);
        });
        if (!found || !complete) return std::nullopt;
        frame = next;
    }
    for (auto& rms : column.rms) rms = std::sqrt(rms / static_cast<float>(end - begin));
    return column;
}

struct composed_waveform_context_t {
    const waveform_t& overview;
    const waveform_t* detail;
    const timing_index_t& timing;
    const aligned_waveform_request_t& request;
    decoded_block_cache_t* blocks;
    aligned_waveform_style_t style;
    long double rows_per_pixel;
};

struct composed_column_t {
    waveform_column_t column;
    std::uint32_t source_level{};
};

auto composed_pixel_column(const composed_waveform_context_t& context, double first_pixel,
                            double last_pixel) -> std::optional<composed_column_t> {
    const auto first_row = std::clamp((context.request.viewport_scroll_pixels + first_pixel) *
        context.rows_per_pixel, 0.0L, static_cast<long double>(context.timing.total_rows));
    const auto last_row = std::clamp((context.request.viewport_scroll_pixels + last_pixel) *
        context.rows_per_pixel, 0.0L, static_cast<long double>(context.timing.total_rows));
    if (first_row >= last_row) return std::nullopt;
    const auto first = frame_at_fractional_row(context.overview, context.timing, first_row, false);
    const auto last = frame_at_fractional_row(context.overview, context.timing, last_row, true);
    if (!first || !last) return std::nullopt;
    const auto begin = static_cast<std::uint64_t>(std::clamp(*first, 0.0L,
        static_cast<long double>(context.overview.total_frames)));
    const auto end = static_cast<std::uint64_t>(std::clamp(*last, 0.0L,
        static_cast<long double>(context.overview.total_frames)));
    if (begin >= end) return std::nullopt;
    if (context.blocks) {
        if (auto pcm = pcm_span(*context.blocks, context.overview.key.source, begin, end))
            return composed_column_t{*pcm, 1};
    }
    if (context.style.detail_level && context.detail &&
        context.detail->key.source == context.overview.key.source) {
        if (auto detail = bin_span(*context.detail, begin, end))
            return composed_column_t{*detail, 32};
    }
    const auto coarse = bin_span(context.overview, begin, end);
    return coarse ? std::optional<composed_column_t>{{*coarse, 256}} : std::nullopt;
}

void append_composed_rows(aligned_waveform_result_t& result, const waveform_column_t& column,
                          double first_pixel, double last_pixel, bool samples) {
    if (samples) {
        const auto span = last_pixel - first_pixel;
        result.rows.push_back({.y_pixels = first_pixel + span * 0.25,
                               .sample_point = true, .samples = column.minimum});
        result.rows.push_back({.y_pixels = first_pixel + span * 0.75,
                               .sample_point = true, .samples = column.maximum});
    } else {
        result.rows.push_back({.y_pixels = (first_pixel + last_pixel) * 0.5,
            .absolute_peak = column.absolute_peak, .rms = column.rms});
    }
}

} // namespace

auto aligned_waveform_detail_range_t::covers(aligned_waveform_window_t window) const noexcept -> bool {
    return begin_frame <= window.begin_frame && end_frame >= window.end_frame;
}

auto aligned_waveform_detail_range_t::needs_refill(aligned_waveform_window_t window,
                                                  int direction) const noexcept -> bool {
    if (!covers(window)) return true;
    const auto span = window.end_frame - window.begin_frame;
    return direction < 0 ? window.begin_frame - begin_frame <= span
                         : end_frame - window.end_frame <= span;
}

auto padded_aligned_waveform_range(aligned_waveform_window_t window, int direction)
    -> std::optional<aligned_waveform_detail_range_t> {
    constexpr std::uint64_t chunk = 65'536;
    constexpr std::uint64_t capacity = maximum_waveform_projection_columns * 32ULL;
    if (window.end_frame <= window.begin_frame) return std::nullopt;
    const auto span = window.end_frame - window.begin_frame;
    if (span > capacity - chunk) return std::nullopt;
    const auto margin = std::min(span, (capacity - span - chunk) / 3U);
    const auto behind = direction < 0 ? margin * 2U : margin;
    const auto ahead = direction < 0 ? margin : margin * 2U;
    auto begin = window.begin_frame - std::min(window.begin_frame, behind);
    begin -= begin % chunk;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    auto end = window.end_frame + std::min(maximum - window.end_frame, ahead);
    const auto padding = (chunk - end % chunk) % chunk;
    end += std::min(maximum - end, padding);
    end = std::min(end, begin + std::min(maximum - begin, capacity));
    if (end < window.end_frame) return std::nullopt;
    return aligned_waveform_detail_range_t{begin, end};
}

void update_aligned_waveform_style(aligned_waveform_style_t& style,
                                   double frames_per_pixel) noexcept {
    if (!std::isfinite(frames_per_pixel) || frames_per_pixel < 0) return;
    if (style.sample_points && frames_per_pixel > 1.1) style.sample_points = false;
    else if (!style.sample_points && frames_per_pixel < 0.9) style.sample_points = true;
    if (style.detail_level && frames_per_pixel > 280) style.detail_level = false;
    else if (!style.detail_level && frames_per_pixel < 230) style.detail_level = true;
}

auto aligned_waveform_window(const waveform_t& waveform, const timing_index_t& timing,
    aligned_waveform_request_t request) -> std::optional<aligned_waveform_window_t> {
    if (!std::isfinite(request.viewport_scroll_pixels) ||
        !std::isfinite(request.viewport_height_pixels) ||
        !std::isfinite(request.pixels_per_standard_measure) ||
        request.viewport_height_pixels <= 0 || request.pixels_per_standard_measure <= 0 ||
        timing.divisions_per_standard_measure == 0 || timing.total_rows == 0 ||
        waveform.sample_rate == 0 || waveform.total_frames == 0) return std::nullopt;
    const auto rows_per_pixel = static_cast<long double>(timing.divisions_per_standard_measure) /
                                request.pixels_per_standard_measure;
    const auto first = std::clamp(request.viewport_scroll_pixels * rows_per_pixel,
                                 0.0L, static_cast<long double>(timing.total_rows));
    const auto last = std::clamp((request.viewport_scroll_pixels + request.viewport_height_pixels) *
                                rows_per_pixel, 0.0L, static_cast<long double>(timing.total_rows));
    if (first >= last) return std::nullopt;
    const auto begin = frame_at_fractional_row(waveform, timing, first, false);
    const auto end = frame_at_fractional_row(waveform, timing, last, true);
    if (!begin || !end || *begin >= *end) return std::nullopt;
    const auto clipped_begin = static_cast<std::uint64_t>(std::clamp(*begin,
        0.0L, static_cast<long double>(waveform.total_frames)));
    const auto clipped_end = static_cast<std::uint64_t>(std::clamp(*end,
        0.0L, static_cast<long double>(waveform.total_frames)));
    if (clipped_begin >= clipped_end) return std::nullopt;
    const auto visible_pixels = (last - first) / rows_per_pixel;
    return aligned_waveform_window_t{clipped_begin, clipped_end,
        static_cast<double>((*end - *begin) / visible_pixels)};
}

auto project_aligned_waveform_samples(const waveform_t& waveform, const timing_index_t& timing,
    aligned_waveform_request_t request, decoded_block_cache_t& cache) -> aligned_waveform_result_t {
    aligned_waveform_result_t result;
    const auto window = aligned_waveform_window(waveform, timing, request);
    if (!window || window->frames_per_pixel >= 1.0 || request.maximum_rows == 0 ||
        request.maximum_rows > maximum_aligned_waveform_rows) return result;
    const auto samples = project_cached_waveform_samples(cache, waveform.key.source,
        window->begin_frame, window->end_frame, request.maximum_rows);
    if (!samples) return result;
    const auto rows_per_pixel = static_cast<long double>(timing.divisions_per_standard_measure) /
                               request.pixels_per_standard_measure;
    for (const auto& sample : *samples) {
        if (sample.frame > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            return result;
        const auto musical = musical_position_at(timing,
            {static_cast<std::int64_t>(sample.frame), waveform.sample_rate});
        if (!musical || !musical.absolute_row) return result;
        const auto y = static_cast<long double>(musical.absolute_row->numerator) /
                       musical.absolute_row->denominator / rows_per_pixel - request.viewport_scroll_pixels;
        if (y >= 0 && y < request.viewport_height_pixels)
            result.rows.push_back({.y_pixels = static_cast<double>(y), .sample_point = true,
                                   .samples = sample.samples});
    }
    result.status = aligned_waveform_status_t::projected;
    return result;
}

auto project_aligned_waveform(const waveform_t& waveform, const timing_index_t& timing,
                              aligned_waveform_request_t request) -> aligned_waveform_result_t {
    aligned_waveform_result_t result;
    if (!std::isfinite(request.viewport_scroll_pixels) ||
        !std::isfinite(request.viewport_height_pixels) ||
        !std::isfinite(request.pixels_per_standard_measure) ||
        request.viewport_height_pixels <= 0.0 ||
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
    result.status = aligned_waveform_status_t::projected;
    const auto window = aligned_waveform_window(waveform, timing, request);
    if (!window) return result;
    const auto projection = project_waveform(
        waveform, {.begin_frame = window->begin_frame,
                   .end_frame = window->end_frame,
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
    const aligned_row_context_t row_context{waveform, timing, request, rows_per_pixel, height};
    for (const auto& column : projection.columns) {
        const auto pixel = pixel_for_column(row_context, column);
        if (!pixel.has_value()) {
            continue;
        }
        occupied[*pixel] = true;
        accumulate_column(pixels[*pixel], column, *pixel);
    }
    result.rows.reserve(height);
    for (std::size_t pixel = 0; pixel < height; ++pixel) {
        if (occupied[pixel]) {
            result.rows.push_back(pixels[pixel]);
        }
    }
    return result;
}

auto project_composed_aligned_waveform(const waveform_t& overview, const waveform_t* detail,
    const timing_index_t& timing, aligned_waveform_request_t request,
    decoded_block_cache_t* blocks, aligned_waveform_style_t style) -> aligned_waveform_result_t {
    aligned_waveform_result_t result;
    if (!aligned_waveform_window(overview, timing, request) || request.maximum_rows == 0 ||
        request.maximum_rows > maximum_aligned_waveform_rows) return result;
    result.chosen_frames_per_bin = style.detail_level ? 32 : 256;
    const auto points = style.sample_points ? 2U : 1U;
    if (request.viewport_height_pixels > maximum_aligned_waveform_rows || request.maximum_rows < points) {
        result.status = aligned_waveform_status_t::capacity_exceeded;
        return result;
    }
    const auto height = static_cast<std::size_t>(std::ceil(request.viewport_height_pixels));
    if (style.sample_points && blocks) {
        auto samples = project_aligned_waveform_samples(overview, timing, request, *blocks);
        if (samples && !samples.rows.empty()) {
            samples.chosen_frames_per_bin = result.chosen_frames_per_bin;
            samples.pcm_spans = 1; // One fully cache-resident span; keep exact source-frame vertices.
            return samples;
        }
    }
    const auto stride = std::max<std::size_t>(1, (height * points + request.maximum_rows - 1U) / request.maximum_rows);
    const composed_waveform_context_t context{overview, detail, timing, request, blocks, style,
        static_cast<long double>(timing.divisions_per_standard_measure) / request.pixels_per_standard_measure};
    for (std::size_t pixel = 0; pixel < height; pixel += stride) {
        const auto end = std::min(pixel + stride, height);
        if (const auto column = composed_pixel_column(context, pixel, end)) {
            if (column->source_level == 1) ++result.pcm_spans;
            else if (column->source_level == 32) ++result.detail_spans;
            else ++result.overview_spans;
            append_composed_rows(result, column->column, pixel, end, style.sample_points);
        }
    }
    result.status = aligned_waveform_status_t::projected;
    return result;
}

auto project_waveform_overview(const waveform_t& waveform, const timing_index_t& timing,
                               aligned_waveform_request_t viewport, std::size_t maximum_rows)
    -> waveform_overview_result_t {
    waveform_overview_result_t result;
    if (!std::isfinite(viewport.viewport_scroll_pixels) ||
        !std::isfinite(viewport.viewport_height_pixels) ||
        !std::isfinite(viewport.pixels_per_standard_measure) ||
        viewport.viewport_height_pixels <= 0.0 ||
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
