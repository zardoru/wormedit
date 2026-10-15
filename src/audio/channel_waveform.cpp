#include <wormedit/core/rational.h>
#include "../core/timing_arithmetic.h"
#include "../core/checked_math.h"
#include <wormedit/audio/channel_waveform.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <numeric>
#include <optional>

namespace wormedit::audio {
namespace {

using checked_math::magnitude;

using timing_arithmetic::try_normalize;

using checked_math::checked_signed_multiply;

using timing_arithmetic::add_exact;

using timing_arithmetic::subtract_exact;

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
    const auto musical = context.timing.row_at(*time);
    if (!musical || !musical.absolute_row || musical.absolute_row->denominator == 0) {
        return std::nullopt;
    }
    const auto row = to_long_double(*musical.absolute_row);
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

auto is_valid_channel_waveform_request(const aligned_waveform_request_t& request) -> bool {
    return std::isfinite(request.viewport_scroll_pixels) &&
           std::isfinite(request.viewport_height_pixels) &&
           std::isfinite(request.pixels_per_standard_measure) &&
           request.viewport_height_pixels > 0.0 &&
           request.pixels_per_standard_measure > 0.0 && request.maximum_rows != 0;
}

auto is_valid_channel_source(const waveform_t& waveform, const sound_channel_t& channel,
                          channel_waveform_source_geometry_t source) -> bool {
    return source.sample_rate != 0 && waveform.sample_rate != 0 &&
           waveform.total_frames != 0 &&
           waveform.total_frames <=
               static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) &&
           (!source.total_frames || channel.source_clip.begin_frame < *source.total_frames);
}

auto is_channel_window_within_capacity(const aligned_waveform_request_t& request) -> bool {
    return request.viewport_height_pixels <= static_cast<double>(request.maximum_rows) &&
           request.viewport_height_pixels <=
               static_cast<double>(maximum_aligned_waveform_rows);
}

struct channel_slice_context_t {
    const waveform_t& waveform;
    const timing_index_t& timing;
    const aligned_waveform_request_t& request;
    long double rows_per_pixel;
    std::size_t height;
    std::uint32_t source_sample_rate;
};

struct row_window_t {
    long double first, last;
};

// Rasterizes one slice's part of the viewport into the shared pixel rows. A slice that has no
// sound in view (outside the viewport, or already past the end of the sample) adds nothing.
auto build_channel_slice_view(const channel_slice_context_t& context, const derived_sound_slice_t& slice,
                           std::optional<std::uint64_t> next_slice_row, row_window_t view,
                           std::vector<aligned_waveform_row_t>& pixels, std::vector<bool>& occupied)
    -> aligned_waveform_status_t {
    const auto slice_end_row = static_cast<long double>(next_slice_row.value_or(context.timing.total_rows));
    const auto low = std::max(view.first, static_cast<long double>(slice.absolute_row));
    const auto high = std::min(view.last, slice_end_row);
    if (low >= high) return aligned_waveform_status_t::projected;
    const auto slice_begin = output_frame(slice.source_begin_frame, context.source_sample_rate, context.waveform);
    const auto slice_end = slice.source_end_frame
        ? output_frame(*slice.source_end_frame, context.source_sample_rate, context.waveform)
        : std::optional{context.waveform.total_frames};
    if (!slice_begin || !slice_end) return aligned_waveform_status_t::invalid_waveform;
    if (*slice_begin >= *slice_end) return aligned_waveform_status_t::projected;
    const auto origin = context.timing.seconds_at(slice.absolute_row);
    const auto begin_time = context.timing.seconds_at(static_cast<std::uint64_t>(low),
                                      timing_boundary_side_t::before_delays);
    const auto end_time = context.timing.seconds_at(static_cast<std::uint64_t>(high),
                                    timing_boundary_side_t::after_stops);
    if (!origin || !origin.seconds || !begin_time || !end_time) return aligned_waveform_status_t::invalid_timing;
    const auto first_frame = source_frame_at(*begin_time.seconds, *origin.seconds, context.waveform,
                                             *slice_begin, sample_rounding_t::floor);
    const auto last_frame = source_frame_at(*end_time.seconds, *origin.seconds, context.waveform,
                                            *slice_begin, sample_rounding_t::ceil);
    if (!first_frame || !last_frame) return aligned_waveform_status_t::invalid_timing;
    const auto begin = std::max(*first_frame, *slice_begin);
    const auto end = std::min(*last_frame, *slice_end);
    if (begin >= end) return aligned_waveform_status_t::projected;
    const auto projection = waveform_view_t::from(
        context.waveform, {.begin_frame = begin, .end_frame = end,
                           .pixel_width = std::min(context.height * 2U, maximum_aligned_waveform_rows),
                           .maximum_columns = maximum_aligned_waveform_rows});
    if (!projection)
        return projection.error() == waveform_projection_status_t::capacity_exceeded
                   ? aligned_waveform_status_t::capacity_exceeded
                   : aligned_waveform_status_t::invalid_waveform;
    const channel_row_context_t row_context{context.waveform, context.timing, context.request, *origin.seconds,
                                            *slice_begin, context.rows_per_pixel, context.height};
    rasterize_channel_columns(row_context, projection->columns, pixels, occupied);
    return aligned_waveform_status_t::projected;
}

} // namespace

auto aligned_waveform_view_result_t::from(const waveform_t& waveform, const chart_document_t& chart,
                              const sound_channel_t& channel, const timing_index_t& timing,
                              aligned_waveform_request_t request,
                              channel_waveform_source_geometry_t source)
    -> aligned_waveform_view_outcome_t {
    const auto slices =
        derive_sound_channel_slices(chart, channel, source.sample_rate, source.total_frames);
    return aligned_waveform_view_result_t::from(waveform, channel, timing, request, slices, source);
}

auto aligned_waveform_view_result_t::from(const waveform_t& waveform, const sound_channel_t& channel,
                              const timing_index_t& timing, aligned_waveform_request_t request,
                              const sound_slice_result_t& slices,
                              channel_waveform_source_geometry_t source)
    -> aligned_waveform_view_outcome_t {
    aligned_waveform_view_result_t result;
    if (!is_valid_channel_waveform_request(request))
        return aligned_waveform_view_result_t::from(std::move(result));
    if (timing.divisions_per_standard_measure == 0 || timing.total_rows == 0 ||
        timing.rows.empty()) {
        result.status = aligned_waveform_status_t::invalid_timing;
        return aligned_waveform_view_result_t::from(std::move(result));
    }
    if (!is_valid_channel_source(waveform, channel, source)) {
        result.status = aligned_waveform_status_t::invalid_waveform;
        return aligned_waveform_view_result_t::from(std::move(result));
    }
    if (!is_channel_window_within_capacity(request)) {
        result.status = aligned_waveform_status_t::capacity_exceeded;
        return aligned_waveform_view_result_t::from(std::move(result));
    }
    if (!slices) {
        result.status = aligned_waveform_status_t::invalid_timing;
        return aligned_waveform_view_result_t::from(std::move(result));
    }
    result.status = aligned_waveform_status_t::projected;
    if (slices.slices.empty())
        return aligned_waveform_view_result_t::from(std::move(result));
    const auto source_begin =
        output_frame(channel.source_clip.begin_frame, source.sample_rate, waveform);
    if (!source_begin || *source_begin >= waveform.total_frames) {
        result.status = aligned_waveform_status_t::invalid_waveform;
        return aligned_waveform_view_result_t::from(std::move(result));
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
        return aligned_waveform_view_result_t::from(std::move(result));
    const auto height = static_cast<std::size_t>(std::ceil(request.viewport_height_pixels));
    std::vector<aligned_waveform_row_t> pixels(height);
    std::vector<bool> occupied(height);
    // Each slice plays its own stretch of the sample from its own anchor (a restart rewinds the
    // audio), so the waveform is projected slice by slice rather than as one linear run.
    for (std::size_t index = 0; index < slices.slices.size(); ++index) {
        const auto status = build_channel_slice_view(
            {waveform, timing, request, rows_per_pixel, height, source.sample_rate},
            slices.slices[index],
            index + 1 < slices.slices.size() ? std::optional{slices.slices[index + 1].absolute_row}
                                             : std::nullopt,
            {first_row, last_row}, pixels, occupied);
        if (status != aligned_waveform_status_t::projected) {
            result.status = status;
            return aligned_waveform_view_result_t::from(std::move(result));
        }
    }
    result.rows.reserve(height);
    for (std::size_t pixel = 0; pixel < height; ++pixel) {
        if (occupied[pixel])
            result.rows.push_back(pixels[pixel]);
    }
    return aligned_waveform_view_result_t::from(std::move(result));
}

namespace {
struct display_channel_slice_t {
    double origin_seconds{}, end_seconds{};
    std::uint64_t begin_frame{}, end_frame{};
};
auto display_output_frame(std::uint64_t frame, std::uint32_t native_rate,
                          const waveform_t& waveform) -> std::optional<std::uint64_t> {
    if (native_rate == 0) return std::nullopt;
    const auto whole = frame / native_rate;
    if (whole > std::numeric_limits<std::uint64_t>::max() / waveform.sample_rate) return std::nullopt;
    const auto product = (frame % native_rate) * waveform.sample_rate;
    const auto base = whole * waveform.sample_rate;
    const auto tail = product / native_rate;
    if (base > std::numeric_limits<std::uint64_t>::max() - tail) return std::nullopt;
    auto result = base + tail;
    const auto remainder = product % native_rate;
    if (remainder * 2 > native_rate || (remainder * 2 == native_rate && result % 2 != 0)) {
        if (result == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
        ++result;
    }
    return std::min(result, waveform.total_frames);
}
auto visible_display_slices(const waveform_t& waveform, const display_row_table_t& table,
    const sound_slice_result_t& slices, channel_waveform_source_geometry_t source)
    -> std::vector<display_channel_slice_t> {
    std::vector<display_channel_slice_t> result;
    if (table.rows.empty() || slices.slices.empty()) return result;
    const auto first_row = table.rows.front().begin_row;
    const auto last_row = table.rows.back().end_row;
    auto after = std::ranges::lower_bound(slices.slices, first_row, {}, &derived_sound_slice_t::absolute_row);
    auto index = after == slices.slices.begin() ? std::size_t{0}
        : static_cast<std::size_t>(std::prev(after) - slices.slices.begin());
    for (; index < slices.slices.size(); ++index) {
        const auto& slice = slices.slices[index];
        if (static_cast<double>(slice.absolute_row) >= last_row) break;
        const auto next = index + 1 < slices.slices.size() ? slices.slices[index + 1].absolute_row : table.total_rows;
        const auto begin = display_output_frame(slice.source_begin_frame, source.sample_rate, waveform);
        const auto end = slice.source_end_frame
            ? display_output_frame(*slice.source_end_frame, source.sample_rate, waveform)
            : std::optional{waveform.total_frames};
        const auto origin = display_seconds_at(table, static_cast<double>(slice.absolute_row));
        const auto finish = display_seconds_at(table, static_cast<double>(next),
            index + 1 < slices.slices.size() ? timing_boundary_side_t::event : timing_boundary_side_t::after_stops);
        if (begin && end && origin && finish && *begin < *end && *origin < *finish)
            result.push_back({*origin, *finish, *begin, *end});
    }
    return result;
}
auto display_source_frame(double seconds, const display_channel_slice_t& slice,
    const waveform_t& waveform, bool end) -> std::uint64_t {
    const auto relative = std::max(0.0, seconds - slice.origin_seconds) * waveform.sample_rate;
    const auto frame = static_cast<double>(slice.begin_frame) + (end ? std::ceil(relative) : std::floor(relative));
    return static_cast<std::uint64_t>(std::clamp(frame, static_cast<double>(slice.begin_frame),
                                                static_cast<double>(slice.end_frame)));
}
auto display_slice_column(const waveform_t& waveform,
    const display_row_time_t& row, const display_channel_slice_t& slice, std::size_t& sampled_bins)
    -> std::optional<waveform_column_t> {
    const auto low = std::max(row.begin_seconds, slice.origin_seconds);
    const auto high = std::min(row.end_seconds, slice.end_seconds);
    if (low >= high) return std::nullopt;
    return sample_display_waveform_span(waveform, display_source_frame(low, slice, waveform, false),
        display_source_frame(high, slice, waveform, true), sampled_bins);
}
auto build_display_channel_row_view(const waveform_t& waveform,
    const display_row_time_t& row, std::span<const display_channel_slice_t> slices,
    std::size_t& cursor, aligned_waveform_view_result_t& result) -> std::optional<aligned_waveform_row_t> {
    while (cursor < slices.size() && slices[cursor].end_seconds <= row.begin_seconds) ++cursor;
    bool occupied = false;
    aligned_waveform_row_t output;
    for (auto index = cursor; index < slices.size() && slices[index].origin_seconds < row.end_seconds; ++index) {
        ++result.visited_slices;
        const auto column = display_slice_column(waveform, row, slices[index], result.sampled_bins);
        if (!column) continue;
        occupied = true;
        for (std::size_t channel = 0; channel < output_channel_count; ++channel) {
            output.absolute_peak[channel] = std::max(output.absolute_peak[channel], column->absolute_peak[channel]);
            output.rms[channel] = std::max(output.rms[channel], column->rms[channel]);
        }
    }
    return occupied ? std::optional{output} : std::nullopt;
}
} // namespace

auto aligned_waveform_view_result_t::from(const waveform_t& waveform, const sound_channel_t& channel,
    const display_row_table_t& table, aligned_waveform_request_t request,
    const sound_slice_result_t& slices, channel_waveform_source_geometry_t source) -> aligned_waveform_view_outcome_t {
    aligned_waveform_view_result_t result;
    if (!is_valid_channel_waveform_request(request)) return aligned_waveform_view_result_t::from(std::move(result));
    if (!table.valid || !slices) { result.status = aligned_waveform_status_t::invalid_timing; return aligned_waveform_view_result_t::from(std::move(result)); }
    if (table.zoom != request.pixels_per_standard_measure) return aligned_waveform_view_result_t::from(std::move(result));
    if (!is_valid_channel_source(waveform, channel, source)) {
        result.status = aligned_waveform_status_t::invalid_waveform; return aligned_waveform_view_result_t::from(std::move(result));
    }
    if (!is_channel_window_within_capacity(request)) {
        result.status = aligned_waveform_status_t::capacity_exceeded; return aligned_waveform_view_result_t::from(std::move(result));
    }
    const auto visible = visible_display_slices(waveform, table, slices, source);
    std::size_t cursor = 0;
    result.rows.reserve(static_cast<std::size_t>(std::ceil(request.viewport_height_pixels)));
    for (const auto& row : table.rows) {
        const auto y = static_cast<double>(row.world_pixel) - request.viewport_scroll_pixels + .5;
        if (!row.valid || y < 0 || y >= request.viewport_height_pixels) continue;
        if (auto projected = build_display_channel_row_view(waveform, row, visible, cursor, result)) {
            projected->y_pixels = y;
            result.rows.push_back(*projected);
        }
    }
    result.status = aligned_waveform_status_t::projected;
    return aligned_waveform_view_result_t::from(std::move(result));
}

} // namespace wormedit::audio
