#include <wormedit/core/rational.h>
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
    const auto before = timing.seconds_at(floor_row,
        fraction == 0.0L && !end ? timing_boundary_side_t::before_delays
                                : timing_boundary_side_t::after_stops);
    if (!before || !before.seconds) return std::nullopt;
    auto seconds = to_long_double(*before.seconds);
    if (fraction != 0.0L) {
        const auto after = timing.seconds_at(floor_row + 1U, timing_boundary_side_t::before_delays);
        if (!after || !after.seconds) return std::nullopt;
        const auto next = to_long_double(*after.seconds);
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
    const auto musical = context.timing.row_at({static_cast<std::int64_t>(midpoint), context.waveform.sample_rate});
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

auto accumulate_column(aligned_waveform_row_t& row, const waveform_column_t& column,
                       std::size_t pixel) -> void {
    row.y_pixels = static_cast<double>(pixel) + 0.5;
    for (std::size_t channel = 0; channel < output_channel_count; ++channel) {
        row.absolute_peak[channel] =
            std::max(row.absolute_peak[channel], column.absolute_peak[channel]);
        row.rms[channel] = std::max(row.rms[channel], column.rms[channel]);
    }
}

auto is_waveform_covering(const waveform_t& waveform, std::uint64_t begin, std::uint64_t end) -> bool {
    const auto finish = waveform.key.end_frame == 0 ? waveform.total_frames : waveform.key.end_frame;
    return waveform.key.frames_per_bin != 0 && begin >= waveform.key.begin_frame && end <= finish;
}

auto bin_span(const waveform_t& waveform, std::uint64_t begin, std::uint64_t end)
    -> std::optional<waveform_column_t> {
    if (!is_waveform_covering(waveform, begin, end) || begin >= end) return std::nullopt;
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

// Cached affine clock inside one uninterrupted timing segment. Boundary rows
// retain the exact query path so pauses and warp edges keep their side semantics.
struct row_frame_cursor_t {
    const timing_segment_t* segment{};
    long double begin_seconds{};
    long double seconds_per_row{};

    void select(const timing_index_t& timing, std::uint64_t row) {
        if (segment && row >= segment->begin_row && row < segment->end_row) return;
        const auto after = std::ranges::upper_bound(timing.segments, row, {},
                                                   &timing_segment_t::begin_row);
        segment = after == timing.segments.begin() ? nullptr : &*std::prev(after);
        if (!segment || segment->begin_seconds.denominator == 0 ||
            segment->end_seconds.denominator == 0 || segment->end_row <= segment->begin_row) {
            segment = nullptr;
            return;
        }
        begin_seconds = to_long_double(segment->begin_seconds);
        const auto finish = to_long_double(segment->end_seconds);
        seconds_per_row = (finish - begin_seconds) / (segment->end_row - segment->begin_row);
    }

    auto frame(const waveform_t& waveform, const timing_index_t& timing,
               long double row, bool end) -> std::optional<long double> {
        const auto floor_row = static_cast<std::uint64_t>(std::floor(row));
        select(timing, floor_row);
        if (!segment || floor_row <= segment->begin_row || row >= segment->end_row ||
            floor_row + 1U > segment->end_row)
            return frame_at_fractional_row(waveform, timing, row, end);
        const auto seconds = begin_seconds + (row - segment->begin_row) * seconds_per_row;
        const auto frame = seconds * waveform.sample_rate;
        if (!std::isfinite(frame)) return std::nullopt;
        return end ? std::ceil(frame) : std::floor(frame);
    }
};

struct composed_waveform_context_t {
    const waveform_t& overview;
    const waveform_t* detail;
    const timing_index_t& timing;
    const aligned_waveform_request_t& request;
    decoded_block_cache_t* blocks;
    aligned_waveform_style_t style;
    long double rows_per_pixel;
    row_frame_cursor_t& clock;
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
    const auto first = context.clock.frame(context.overview, context.timing, first_row, false);
    const auto last = context.clock.frame(context.overview, context.timing, last_row, true);
    if (!first || !last) return std::nullopt;
    const auto begin = static_cast<std::uint64_t>(std::clamp(*first, 0.0L,
        static_cast<long double>(context.overview.total_frames)));
    const auto end = static_cast<std::uint64_t>(std::clamp(*last, 0.0L,
        static_cast<long double>(context.overview.total_frames)));
    if (begin >= end) return std::nullopt;
    if (context.blocks && (context.style.sample_points || context.style.detail_level)) {
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

void append_composed_rows(aligned_waveform_view_result_t& result, const waveform_column_t& column,
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

namespace {
using wormedit::to_double;
auto is_display_clock_current(const display_row_table_t& table, const timing_index_t& timing,
                           display_row_table_identity_t identity) -> bool {
    return table.valid && table.identity == identity && table.timing_identity == &timing &&
        table.fingerprint == timing.source_fingerprint &&
        table.divisions == timing.divisions_per_standard_measure && table.total_rows == timing.total_rows &&
        table.profile == timing.discontinuity_profile && table.bounded_clock == timing.bounded_clock &&
        table.clock_roundings == timing.bounded_clock_roundings &&
        table.timing_row_count == timing.rows.size() && table.timing_segment_count == timing.segments.size() &&
        table.timing_rows_identity == timing.rows.data() && table.timing_segments_identity == timing.segments.data();
}
void rebuild_display_clock(display_row_table_t& table, const timing_index_t& timing,
                           display_row_table_identity_t identity) {
    table.identity = identity;
    table.timing_identity = &timing;
    table.fingerprint = timing.source_fingerprint;
    table.divisions = timing.divisions_per_standard_measure;
    table.total_rows = timing.total_rows;
    table.profile = timing.discontinuity_profile;
    table.bounded_clock = timing.bounded_clock;
    table.clock_roundings = timing.bounded_clock_roundings;
    table.timing_row_count = timing.rows.size();
    table.timing_segment_count = timing.segments.size();
    table.timing_rows_identity = timing.rows.data();
    table.timing_segments_identity = timing.segments.data();
    table.boundaries.clear();
    table.segments.clear();
    table.boundaries.reserve(timing.rows.size());
    table.segments.reserve(timing.segments.size());
    ++table.clock_generation;
    for (const auto& row : timing.rows)
        table.boundaries.push_back({static_cast<double>(row.absolute_row),
            to_double(row.before_delays_seconds), to_double(row.event_seconds),
            to_double(row.after_stops_seconds)});
    for (const auto& segment : timing.segments) {
        const auto begin = to_double(segment.begin_seconds);
        const auto distance = static_cast<double>(segment.end_row) - static_cast<double>(segment.begin_row);
        table.segments.push_back({static_cast<double>(segment.begin_row),
            static_cast<double>(segment.end_row), begin,
            distance > 0 ? (to_double(segment.end_seconds) - begin) / distance : 0.0});
    }
}
auto is_valid_display_request(aligned_waveform_request_t request, bool padding = true) -> bool {
    return std::isfinite(request.viewport_scroll_pixels) && std::abs(request.viewport_scroll_pixels) <= 1e12 &&
        std::isfinite(request.viewport_height_pixels) && request.viewport_height_pixels > 0 &&
        request.viewport_height_pixels <= maximum_aligned_waveform_rows - (padding ? 2 : 0) &&
        request.viewport_height_pixels <= static_cast<double>(request.maximum_rows) &&
        std::isfinite(request.pixels_per_standard_measure) && request.pixels_per_standard_measure > 0;
}
[[maybe_unused]] auto make_display_row(const display_row_table_t& table, std::int64_t world)
    -> display_row_time_t {
    return display_row_time_t::from(table, world);
}

} // namespace

auto display_row_time_t::from(const display_row_table_t& table, std::int64_t world)
    -> display_row_time_t {
    const auto scale = static_cast<double>(table.divisions) / table.zoom;
    const auto begin = std::clamp(static_cast<double>(world) * scale, 0.0, static_cast<double>(table.total_rows));
    const auto end = std::clamp((static_cast<double>(world) + 1.0) * scale, 0.0, static_cast<double>(table.total_rows));
    const auto first = display_seconds_at(table, begin, timing_boundary_side_t::before_delays);
    const auto last = display_seconds_at(table, end, timing_boundary_side_t::before_delays);
    return {world, begin, end, first.value_or(0.0), last.value_or(0.0),
            begin < end && first && last && *first <= *last};
}

auto display_seconds_at(const display_row_table_t& table, double row, timing_boundary_side_t side)
    -> std::optional<double> {
    if (!std::isfinite(row) || row < 0 || row > static_cast<double>(table.total_rows)) return std::nullopt;
    const auto boundary = std::ranges::lower_bound(table.boundaries, row, {}, &display_timing_boundary_t::row);
    double seconds{};
    if (boundary != table.boundaries.end() && boundary->row == row) {
        seconds = side == timing_boundary_side_t::before_delays ? boundary->before_seconds
            : (side == timing_boundary_side_t::after_stops ? boundary->after_seconds : boundary->event_seconds);
    } else {
        const auto after = std::ranges::upper_bound(table.segments, row, {}, &display_timing_segment_t::begin_row);
        if (after == table.segments.begin()) return std::nullopt;
        const auto& segment = *std::prev(after);
        if (row >= segment.end_row) return std::nullopt;
        seconds = segment.begin_seconds + (row - segment.begin_row) * segment.seconds_per_row;
    }
    return std::isfinite(seconds) ? std::optional{seconds} : std::nullopt;
}

auto prepare_display_row_table(display_row_table_t& table, const timing_index_t& timing,
    aligned_waveform_request_t request, display_row_table_identity_t identity) -> bool {
    if (!is_valid_display_request(request) || timing.divisions_per_standard_measure == 0 ||
        timing.total_rows == 0 || timing.rows.empty()) {
        table.valid = false;
        table.rows.clear();
        return false;
    }
    const bool same_clock = is_display_clock_current(table, timing, identity);
    if (!same_clock) rebuild_display_clock(table, timing, identity);
    const auto count = static_cast<std::size_t>(std::ceil(request.viewport_height_pixels)) + 2;
    const auto first = static_cast<std::int64_t>(std::floor(request.viewport_scroll_pixels)) - 1;
    if (same_clock && table.zoom == request.pixels_per_standard_measure &&
        table.scroll == request.viewport_scroll_pixels && table.height == request.viewport_height_pixels) return true;
    const bool reuse = same_clock && table.zoom == request.pixels_per_standard_measure &&
        table.height == request.viewport_height_pixels;
    auto previous = std::move(table.rows);
    table.rows.clear();
    const auto old_first = table.first_world_pixel;
    table.zoom = request.pixels_per_standard_measure;
    table.scroll = request.viewport_scroll_pixels;
    table.height = request.viewport_height_pixels;
    table.first_world_pixel = first;
    table.built_rows = table.reused_rows = 0;
    table.rows.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto world = first + static_cast<std::int64_t>(index);
        const auto old_index = world - old_first;
        if (reuse && old_index >= 0 && static_cast<std::uint64_t>(old_index) < previous.size()) {
            table.rows.push_back(previous[static_cast<std::size_t>(old_index)]);
            ++table.reused_rows;
        } else {
            table.rows.push_back(display_row_time_t::from(table, world));
            ++table.built_rows;
        }
    }
    ++table.generation;
    table.valid = true;
    return true;
}

namespace {
auto display_level_view(const waveform_t& waveform, std::size_t depth) -> waveform_level_view_t {
    if (depth == 0) return {waveform.key.frames_per_bin, waveform.bins};
    const auto& level = waveform.coarser_levels[depth - 1];
    return {level.frames_per_bin, level.bins};
}
auto display_waveform_level(const waveform_t& waveform, std::uint64_t frames) -> std::size_t {
    const auto upper = std::ranges::lower_bound(waveform.coarser_levels, frames, {},
                                               &waveform_level_t::frames_per_bin);
    const auto lower = static_cast<std::size_t>(upper - waveform.coarser_levels.begin());
    if (upper == waveform.coarser_levels.end()) return lower;
    const auto target = static_cast<long double>(frames);
    if (target * target < static_cast<long double>(display_level_view(waveform, lower).frames_per_bin) * upper->frames_per_bin)
        return lower;
    return lower + 1;
}
void accumulate_display_bin(waveform_column_t& result, const waveform_bin_t& bin,
                            std::size_t& sampled_bins) {
    ++sampled_bins;
    for (std::size_t channel = 0; channel < output_channel_count; ++channel) {
        result.minimum[channel] = std::min(result.minimum[channel], bin.minimum[channel]);
        result.maximum[channel] = std::max(result.maximum[channel], bin.maximum[channel]);
        result.absolute_peak[channel] = std::max(result.absolute_peak[channel], bin.absolute_peak[channel]);
        result.rms[channel] = std::max(result.rms[channel], bin.rms[channel]);
    }
}
auto collect_display_bin(const waveform_t& waveform, std::size_t depth, std::uint64_t index,
    waveform_column_t& result, std::size_t& sampled_bins) -> bool {
    const auto level = display_level_view(waveform, depth);
    if (index >= level.bins.size() || level.frames_per_bin == 0) return false;
    if (index > (waveform.total_frames - waveform.key.begin_frame) / level.frames_per_bin) return false;
    const auto& bin = level.bins[static_cast<std::size_t>(index)];
    const auto first = waveform.key.begin_frame + index * level.frames_per_bin;
    const auto last = first + std::min(bin.frame_count, waveform.total_frames - first);
    if (last <= result.begin_frame || first >= result.end_frame) return true;
    if (depth == 0 || (first >= result.begin_frame && last <= result.end_frame)) {
        accumulate_display_bin(result, bin, sampled_bins);
        return true;
    }
    // Only the two range edges descend. Fully covered interior bins stay coarse,
    // so clip/pixel boundaries cannot import a peak from outside this voice.
    const auto children = display_level_view(waveform, depth - 1);
    if (children.frames_per_bin == 0 || children.frames_per_bin > std::numeric_limits<std::uint64_t>::max() / 2 ||
        level.frames_per_bin != children.frames_per_bin * 2) return false;
    for (auto child = index * 2; child < std::min(index * 2 + 2, static_cast<std::uint64_t>(children.bins.size())); ++child)
        if (!collect_display_bin(waveform, depth - 1, child, result, sampled_bins)) return false;
    return true;
}
} // namespace

auto sample_display_waveform_span(const waveform_t& waveform, std::uint64_t begin,
    std::uint64_t end, std::size_t& sampled_bins) -> std::optional<waveform_column_t> {
    const auto available_end = waveform.key.end_frame == 0 ? waveform.total_frames : waveform.key.end_frame;
    if (begin >= end || begin < waveform.key.begin_frame || end > available_end ||
        available_end > waveform.total_frames || waveform.key.frames_per_bin == 0 || waveform.bins.empty() ||
        waveform.coarser_levels.size() > std::numeric_limits<std::uint64_t>::digits)
        return std::nullopt;
    // Half-pixel buckets keep conservative peak expansion within one row,
    // matching the reference rasterizer's two audio columns per pixel.
    const auto depth = display_waveform_level(waveform, std::max<std::uint64_t>(1, (end - begin) / 2));
    const auto level = display_level_view(waveform, depth);
    if (level.frames_per_bin == 0) return std::nullopt;
    const auto first = (begin - waveform.key.begin_frame) / level.frames_per_bin;
    const auto last = (end - 1 - waveform.key.begin_frame) / level.frames_per_bin;
    if (last >= level.bins.size()) return std::nullopt;
    waveform_column_t result{.begin_frame = begin, .end_frame = end};
    result.minimum.fill(std::numeric_limits<float>::infinity());
    result.maximum.fill(-std::numeric_limits<float>::infinity());
    for (auto index = first; index <= last; ++index)
        if (!collect_display_bin(waveform, depth, index, result, sampled_bins)) return std::nullopt;
    return result;
}

auto aligned_waveform_detail_range_t::is_covering_window(aligned_waveform_window_t window) const noexcept -> bool {
    return begin_frame <= window.begin_frame && end_frame >= window.end_frame;
}

auto aligned_waveform_detail_range_t::is_refill_needed(aligned_waveform_window_t window,
                                                  int direction) const noexcept -> bool {
    if (!is_covering_window(window)) return true;
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
    const auto first = std::clamp(std::floor(request.viewport_scroll_pixels * rows_per_pixel),
                                 0.0L, static_cast<long double>(timing.total_rows));
    const auto last = std::clamp(std::ceil((request.viewport_scroll_pixels + request.viewport_height_pixels) *
                                rows_per_pixel), 0.0L, static_cast<long double>(timing.total_rows));
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

auto aligned_waveform_view_result_t::from(aligned_waveform_view_result_t&& report)
    -> aligned_waveform_view_outcome_t {
    if (report.status != aligned_waveform_status_t::projected)
        return std::unexpected(std::move(report));
    return std::move(report);
}
auto waveform_overview_view_result_t::from(waveform_overview_view_result_t&& report)
    -> waveform_overview_view_outcome_t {
    if (report.status != aligned_waveform_status_t::projected)
        return std::unexpected(std::move(report));
    return std::move(report);
}

auto aligned_waveform_view_result_t::from(const waveform_t& waveform, const timing_index_t& timing,
                                     aligned_waveform_request_t request,
                                     decoded_block_cache_t& cache) -> aligned_waveform_view_outcome_t {
    aligned_waveform_view_result_t result;
    const auto window = aligned_waveform_window(waveform, timing, request);
    if (!window || window->frames_per_pixel >= 1.0 || request.maximum_rows == 0 ||
        request.maximum_rows > maximum_aligned_waveform_rows)
        return from(std::move(result));
    const auto samples = build_cached_waveform_samples_view(
        cache, waveform.key.source, window->begin_frame, window->end_frame, request.maximum_rows);
    if (!samples)
        return from(std::move(result));
    const auto rows_per_pixel = static_cast<long double>(timing.divisions_per_standard_measure) /
                                request.pixels_per_standard_measure;
    for (const auto& sample : *samples) {
        if (sample.frame > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            return from(std::move(result));
        const auto musical =
            timing.row_at({static_cast<std::int64_t>(sample.frame), waveform.sample_rate});
        if (!musical || !musical.absolute_row)
            return from(std::move(result));
        const auto y =
            to_long_double(*musical.absolute_row) / rows_per_pixel - request.viewport_scroll_pixels;
        if (y >= 0 && y < request.viewport_height_pixels)
            result.rows.push_back({.y_pixels = static_cast<double>(y),
                                   .sample_point = true,
                                   .samples = sample.samples});
    }
    result.status = aligned_waveform_status_t::projected;
    return from(std::move(result));
}

auto aligned_waveform_view_result_t::from(const waveform_t& waveform, const timing_index_t& timing,
                                     aligned_waveform_request_t request)
    -> aligned_waveform_view_outcome_t {
    aligned_waveform_view_result_t result;
    if (!std::isfinite(request.viewport_scroll_pixels) ||
        !std::isfinite(request.viewport_height_pixels) ||
        !std::isfinite(request.pixels_per_standard_measure) ||
        request.viewport_height_pixels <= 0.0 || request.pixels_per_standard_measure <= 0.0 ||
        request.maximum_rows == 0) {
        return from(std::move(result));
    }
    if (timing.divisions_per_standard_measure == 0 || timing.total_rows == 0 ||
        timing.rows.empty()) {
        result.status = aligned_waveform_status_t::invalid_timing;
        return from(std::move(result));
    }
    if (waveform.sample_rate == 0 || waveform.total_frames == 0 ||
        waveform.total_frames >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        result.status = aligned_waveform_status_t::invalid_waveform;
        return from(std::move(result));
    }
    if (request.viewport_height_pixels > static_cast<double>(request.maximum_rows) ||
        request.viewport_height_pixels > static_cast<double>(maximum_aligned_waveform_rows)) {
        result.status = aligned_waveform_status_t::capacity_exceeded;
        return from(std::move(result));
    }
    const auto height = static_cast<std::size_t>(std::ceil(request.viewport_height_pixels));
    const auto rows_per_pixel = static_cast<long double>(timing.divisions_per_standard_measure) /
                                request.pixels_per_standard_measure;
    result.status = aligned_waveform_status_t::projected;
    const auto window = aligned_waveform_window(waveform, timing, request);
    if (!window)
        return from(std::move(result));
    const auto projection = waveform_view_t::from(
        waveform, {.begin_frame = window->begin_frame,
                   .end_frame = window->end_frame,
                   .pixel_width = std::min(height * 2U, maximum_aligned_waveform_rows),
                   .maximum_columns = maximum_aligned_waveform_rows});
    if (!projection) {
        result.status = projection.error() == waveform_projection_status_t::capacity_exceeded
                            ? aligned_waveform_status_t::capacity_exceeded
                            : aligned_waveform_status_t::invalid_waveform;
        return from(std::move(result));
    }
    std::vector<aligned_waveform_row_t> pixels(height);
    std::vector<bool> occupied(height);
    const aligned_row_context_t row_context{waveform, timing, request, rows_per_pixel, height};
    for (const auto& column : projection->columns) {
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
    return from(std::move(result));
}

auto aligned_waveform_view_result_t::from(const waveform_t& overview, const waveform_t* detail,
                                     const timing_index_t& timing,
                                     aligned_waveform_request_t request,
                                     decoded_block_cache_t* blocks, aligned_waveform_style_t style)
    -> aligned_waveform_view_outcome_t {
    aligned_waveform_view_result_t result;
    if (!aligned_waveform_window(overview, timing, request) || request.maximum_rows == 0 ||
        request.maximum_rows > maximum_aligned_waveform_rows)
        return from(std::move(result));
    result.chosen_frames_per_bin = style.detail_level ? 32 : 256;
    const auto points = style.sample_points ? 2U : 1U;
    if (request.viewport_height_pixels > maximum_aligned_waveform_rows ||
        request.maximum_rows < points) {
        result.status = aligned_waveform_status_t::capacity_exceeded;
        return from(std::move(result));
    }
    const auto height = static_cast<std::size_t>(std::ceil(request.viewport_height_pixels));
    if (style.sample_points && blocks) {
        auto samples = aligned_waveform_view_result_t::from(overview, timing, request, *blocks);
        if (samples && !samples->rows.empty()) {
            samples->chosen_frames_per_bin = result.chosen_frames_per_bin;
            samples->pcm_spans =
                1; // One fully cache-resident span; keep exact source-frame vertices.
            return samples;
        }
    }
    const auto stride = std::max<std::size_t>(1, (height * points + request.maximum_rows - 1U) /
                                                     request.maximum_rows);
    row_frame_cursor_t clock;
    const composed_waveform_context_t context{
        overview,
        detail,
        timing,
        request,
        blocks,
        style,
        static_cast<long double>(timing.divisions_per_standard_measure) /
            request.pixels_per_standard_measure,
        clock};
    for (std::size_t pixel = 0; pixel < height; pixel += stride) {
        const auto end = std::min(pixel + stride, height);
        if (const auto column = composed_pixel_column(context, pixel, end)) {
            if (column->source_level == 1)
                ++result.pcm_spans;
            else if (column->source_level == 32)
                ++result.detail_spans;
            else
                ++result.overview_spans;
            append_composed_rows(result, column->column, pixel, end, style.sample_points);
        }
    }
    result.status = aligned_waveform_status_t::projected;
    return from(std::move(result));
}

namespace {
auto display_song_column(const waveform_t& overview, const waveform_t* detail,
    const display_row_time_t& row, decoded_block_cache_t* blocks,
    aligned_waveform_style_t style, aligned_waveform_view_result_t& result)
    -> std::optional<composed_column_t> {
    const auto first = std::clamp(std::floor(row.begin_seconds * overview.sample_rate),
        0.0, static_cast<double>(overview.total_frames));
    const auto last = std::clamp(std::ceil(row.end_seconds * overview.sample_rate),
        0.0, static_cast<double>(overview.total_frames));
    if (first >= last) return std::nullopt;
    const auto begin = static_cast<std::uint64_t>(first);
    const auto end = static_cast<std::uint64_t>(last);
    if (blocks && style.detail_level && end - begin <= 512) {
        if (auto pcm = pcm_span(*blocks, overview.key.source, begin, end)) return composed_column_t{*pcm, 1};
    }
    if (detail && style.detail_level && detail->key.source == overview.key.source) {
        if (auto column = sample_display_waveform_span(*detail, begin, end, result.sampled_bins))
            return composed_column_t{*column, 32};
    }
    const auto column = sample_display_waveform_span(overview, begin, end, result.sampled_bins);
    return column ? std::optional<composed_column_t>{{*column, 256}} : std::nullopt;
}
} // namespace

auto aligned_waveform_view_result_t::from(const waveform_t& overview, const waveform_t* detail,
                                     const display_row_table_t& table,
                                     aligned_waveform_request_t request,
                                     decoded_block_cache_t* blocks, aligned_waveform_style_t style)
    -> aligned_waveform_view_outcome_t {
    aligned_waveform_view_result_t result;
    if (!table.valid || !is_valid_display_request(request, false) ||
        table.zoom != request.pixels_per_standard_measure)
        return from(std::move(result));
    // Exact cached PCM polylines retain their existing sample-position path.
    if (style.sample_points && table.timing_identity)
        return from(overview, detail, *table.timing_identity, request, blocks, style);
    result.chosen_frames_per_bin = style.detail_level ? 32 : 256;
    for (const auto& row : table.rows) {
        const auto y = static_cast<double>(row.world_pixel) - request.viewport_scroll_pixels + .5;
        if (!row.valid || y < 0 || y >= request.viewport_height_pixels)
            continue;
        const auto column = display_song_column(overview, detail, row, blocks, style, result);
        if (!column)
            continue;
        if (column->source_level == 1)
            ++result.pcm_spans;
        else if (column->source_level == 32)
            ++result.detail_spans;
        else
            ++result.overview_spans;
        result.rows.push_back({.y_pixels = y,
                               .absolute_peak = column->column.absolute_peak,
                               .rms = column->column.rms});
    }
    result.status = aligned_waveform_status_t::projected;
    return from(std::move(result));
}

auto waveform_overview_view_result_t::from(const waveform_t& waveform, const timing_index_t& timing,
                                      aligned_waveform_request_t viewport, std::size_t maximum_rows)
    -> waveform_overview_view_outcome_t {
    waveform_overview_view_result_t result;
    if (!std::isfinite(viewport.viewport_scroll_pixels) ||
        !std::isfinite(viewport.viewport_height_pixels) ||
        !std::isfinite(viewport.pixels_per_standard_measure) ||
        viewport.viewport_height_pixels <= 0.0 || viewport.pixels_per_standard_measure <= 0.0 ||
        maximum_rows == 0) {
        return from(std::move(result));
    }
    if (timing.divisions_per_standard_measure == 0 || timing.total_rows == 0 ||
        timing.rows.empty()) {
        result.status = aligned_waveform_status_t::invalid_timing;
        return from(std::move(result));
    }
    if (waveform.sample_rate == 0 || waveform.total_frames == 0 ||
        waveform.total_frames >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        result.status = aligned_waveform_status_t::invalid_waveform;
        return from(std::move(result));
    }
    if (maximum_rows > maximum_waveform_overview_rows) {
        result.status = aligned_waveform_status_t::capacity_exceeded;
        return from(std::move(result));
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
    const auto begin_time = timing.seconds_at(static_cast<std::uint64_t>(first_row),
                                              timing_boundary_side_t::before_delays);
    const auto end_time = timing.seconds_at(static_cast<std::uint64_t>(last_row),
                                            timing_boundary_side_t::after_stops);
    if (!begin_time || !end_time) {
        result.status = aligned_waveform_status_t::invalid_timing;
        return from(std::move(result));
    }
    const auto begin = frame_at(*begin_time.seconds, waveform.sample_rate, waveform.total_frames,
                                sample_rounding_t::floor);
    const auto end = frame_at(*end_time.seconds, waveform.sample_rate, waveform.total_frames,
                              sample_rounding_t::ceil);
    if (!begin || !end) {
        result.status = aligned_waveform_status_t::invalid_timing;
        return from(std::move(result));
    }
    result.visible_begin_fraction = static_cast<double>(*begin) / waveform.total_frames;
    result.visible_end_fraction = static_cast<double>(*end) / waveform.total_frames;
    const auto beat_zero = timing.seconds_at(0, timing_boundary_side_t::before_delays);
    if (!beat_zero || !beat_zero.seconds || beat_zero.seconds->denominator == 0) {
        result.status = aligned_waveform_status_t::invalid_timing;
        return from(std::move(result));
    }
    const auto beat_zero_fraction =
        to_long_double(*beat_zero.seconds) * waveform.sample_rate / waveform.total_frames;
    if (std::isfinite(beat_zero_fraction) && beat_zero_fraction >= 0.0L &&
        beat_zero_fraction <= 1.0L) {
        result.beat_zero_fraction = static_cast<double>(beat_zero_fraction);
    }
    const auto projection = waveform_view_t::from(waveform, {.begin_frame = 0,
                                                           .end_frame = waveform.total_frames,
                                                           .pixel_width = maximum_rows,
                                                           .maximum_columns = maximum_rows});
    if (!projection) {
        result.status = projection.error() == waveform_projection_status_t::capacity_exceeded
                            ? aligned_waveform_status_t::capacity_exceeded
                            : aligned_waveform_status_t::invalid_waveform;
        return from(std::move(result));
    }
    result.rows.reserve(projection->columns.size());
    for (const auto& column : projection->columns) {
        const auto midpoint = column.begin_frame + (column.end_frame - column.begin_frame) / 2U;
        result.rows.push_back({static_cast<double>(midpoint) / waveform.total_frames,
                               std::max(column.absolute_peak[0], column.absolute_peak[1])});
    }
    result.status = aligned_waveform_status_t::projected;
    return from(std::move(result));
}

auto waveform_overview_view_result_t::from(const waveform_t& waveform, double begin_fraction,
                                      double end_fraction, std::size_t maximum_rows)
    -> waveform_overview_view_outcome_t {
    waveform_overview_view_result_t result;
    if (!std::isfinite(begin_fraction) || !std::isfinite(end_fraction) || begin_fraction < 0.0 ||
        end_fraction > 1.0 || begin_fraction >= end_fraction || maximum_rows == 0)
        return from(std::move(result));
    if (maximum_rows > maximum_waveform_range_rows) {
        result.status = aligned_waveform_status_t::capacity_exceeded;
        return from(std::move(result));
    }
    if (waveform.sample_rate == 0 || waveform.total_frames == 0) {
        result.status = aligned_waveform_status_t::invalid_waveform;
        return from(std::move(result));
    }
    const auto total = static_cast<long double>(waveform.total_frames);
    const auto begin_frame = static_cast<std::uint64_t>(std::floor(begin_fraction * total));
    const auto end_frame = std::min<std::uint64_t>(
        waveform.total_frames, static_cast<std::uint64_t>(std::ceil(end_fraction * total)));
    if (begin_frame >= end_frame) {
        result.status = aligned_waveform_status_t::invalid_waveform;
        return from(std::move(result));
    }
    const auto projection = waveform_view_t::from(waveform, {.begin_frame = begin_frame,
                                                           .end_frame = end_frame,
                                                           .pixel_width = maximum_rows,
                                                           .maximum_columns = maximum_rows});
    if (!projection) {
        result.status = projection.error() == waveform_projection_status_t::capacity_exceeded
                            ? aligned_waveform_status_t::capacity_exceeded
                            : aligned_waveform_status_t::invalid_waveform;
        return from(std::move(result));
    }
    result.visible_begin_fraction = begin_fraction;
    result.visible_end_fraction = end_fraction;
    result.rows.reserve(projection->columns.size());
    for (const auto& column : projection->columns) {
        const auto midpoint = column.begin_frame + (column.end_frame - column.begin_frame) / 2U;
        result.rows.push_back({static_cast<double>(midpoint) / waveform.total_frames,
                               std::max(column.absolute_peak[0], column.absolute_peak[1])});
    }
    result.status = aligned_waveform_status_t::projected;
    return from(std::move(result));
}

} // namespace wormedit::audio
