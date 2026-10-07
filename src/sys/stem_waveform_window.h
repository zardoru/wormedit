#pragma once

#include <wormedit/audio/source_registry.h>
#include <wormedit/core/document.h>
#include <wormedit/core/timing.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <unordered_map>
#include <variant>
#include <vector>

namespace wormedit::sys {

// A BMSON chart can have dozens of stems, but only the ones playing near the cursor are worth
// decoding and drawing. The window is measured in chart measures around the cursor's measure.
inline constexpr std::size_t maximum_visible_stems = 192;

// Measures to look at on each side of the cursor: what the viewport shows at the current zoom,
// plus a margin, so a sliced chart with a hundred tiny stems only decodes the ones in view.
[[nodiscard]] inline auto stem_window_measures(double viewport_height_pixels, double pixels_per_measure)
    -> std::size_t {
    if (!(pixels_per_measure > 0.0) || !(viewport_height_pixels > 0.0)) return 8;
    const auto shown = std::ceil(viewport_height_pixels / pixels_per_measure);
    return static_cast<std::size_t>(std::clamp(shown, 1.0, 64.0)) + 2;
}
[[nodiscard]] inline auto all_stem_channels( const chart_document_t& chart) -> std::vector<const sound_channel_t*> {
    std::vector<const sound_channel_t*> result;
    for (const auto& channel : chart.sound_channels) {
        if (result.size() == maximum_visible_stems)
            break;
        result.push_back(&channel);
    }
    return result;
}

[[nodiscard]] inline auto stem_seconds_at(const timing_index_t& timing, std::uint64_t row)
    -> std::optional<double> {
    const auto query = timing_at(timing, row, timing_boundary_side_t::after_stops);
    if (!query.seconds || !query.seconds->denominator)
        return std::nullopt;
    return static_cast<double>(query.seconds->numerator) /
                                            static_cast<double>(query.seconds->denominator);
}

[[nodiscard]] inline auto
stem_duration_seconds(const song_document_t& document, const sound_channel_t& channel,
                      std::span<const audio::decoded_source_binding_t> bindings)
    -> std::optional <
    double> {
    const auto asset = std::ranges::find(document.audio_assets, channel.source_clip.source, &audio_asset_t::id);
    auto rate =asset == document.audio_assets.end() ? 0U : asset->stream.sample_rate;
    auto frames = asset == document.audio_assets.end() ? std::nullopt : asset->stream.frame_count;
    if (rate == 0 || !frames) {
        const auto binding = std::ranges::find(bindings, channel.source_clip.source,
                                               &audio::decoded_source_binding_t::asset);
        if (binding != bindings.end()) {
            if (!rate)
                rate = binding->source_sample_rate;
            if (!frames)
                frames = binding->source_frame_count;
        }
    }
    if (!rate || !frames)
        return std::nullopt;
    const auto end = std::min(channel.source_clip.end_frame.value_or(*frames), *frames);
    if (end <= channel.source_clip.begin_frame)
        return 0.0;
    return static_cast<double>(end - channel.source_clip.begin_frame) / rate;
}

[[nodiscard]] inline auto stem_interval_overlaps(double begin, double end, double window_begin,
                                                 double window_end) -> bool {
    return begin < window_end && end > window_begin;
}

struct stem_slice_scan_t {
    std::optional<double> restart_time;
    std::optional<double> previous_time;
};

[[nodiscard]] inline auto stem_event_reaches_window(const sound_channel_event_t& event,
                                                    std::size_t measure_index, double event_time,
                                                    stem_slice_scan_t& scan, std::size_t first,
                                                    std::size_t last, double window_begin,
                                                    double window_end,
                                                    std::optional<double> duration) -> bool {
    if (scan.previous_time && event_time < *scan.previous_time)
        return true;
    if (scan.previous_time) {
        const auto end =
            duration ? std::min(event_time, *scan.restart_time + *duration) : event_time;
        if (stem_interval_overlaps(*scan.previous_time, end, window_begin, window_end))
            return true;
    }
    if (!event.continuation || !scan.restart_time)
        scan.restart_time = event_time;
    scan.previous_time = event_time;
    return measure_index >= first && measure_index < last &&
           (!duration || event_time < *scan.restart_time + *duration);
}

[[nodiscard]] inline auto stem_channel_reaches_window(
    const sound_channel_t& channel, const std::unordered_map<std::uint64_t, std::size_t>& index_of,
    const measure_index_t& measures, const timing_index_t& timing, std::size_t first,
    std::size_t last, double window_begin, double window_end, double chart_end,
    std::optional<double> duration) -> bool {
    stem_slice_scan_t scan;
    for (const auto& event : channel.events) {
        const auto at = index_of.find(event.position.measure.value);
        const auto row =
            at == index_of.end() ? std::nullopt : absolute_row(measures, event.position);
        const auto event_time = row ? stem_seconds_at(timing, *row) : std::nullopt;
        if (!event_time)
            return true;
        if (stem_event_reaches_window(event, at == index_of.end() ? 0 : at->second, *event_time,
                                      scan, first, last, window_begin, window_end, duration))
            return true;
    }
    if (!scan.previous_time || !scan.restart_time)
        return false;
    const auto end = duration ? std::min(chart_end, *scan.restart_time + *duration) : chart_end;
    return stem_interval_overlaps(*scan.previous_time, end, window_begin, window_end);
}

// The sound channels that can be heard in the window, in chart order, at most
// `maximum_visible_stems`: those with an event inside it, or an earlier one whose audio is long
// enough to reach it. A cursor outside the chart selects the first measures.
[[nodiscard]] inline auto visible_stem_channels(
    const song_document_t& document, const chart_document_t& chart, measure_id_t cursor,
    std::size_t window_measures, std::span<const audio::decoded_source_binding_t> bindings = {},
    const timing_index_t* cached_timing = nullptr) -> std::vector<const sound_channel_t*> {
    std::unordered_map<std::uint64_t, std::size_t> index_of;
    index_of.reserve(chart.measures.size());
    for (std::size_t index = 0; index < chart.measures.size(); ++index)
        index_of.emplace(chart.measures[index].id.value, index);
    const auto found = index_of.find(cursor.value);
    const auto center = found == index_of.end() ? std::size_t{0} : found->second;
    const auto first = center > window_measures ? center - window_measures : 0;
    const auto last = std::min(chart.measures.size(), center + window_measures + 1);
    const auto measure_result = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(measure_result))
        return all_stem_channels(chart);
    const auto& measures = std::get<measure_index_t>(measure_result);
    std::optional<timing_index_t> built_timing;
    if (!cached_timing) {
        auto result = build_timing_index(chart, {.allow_bounded_clock = true});
        if (!result)
            return all_stem_channels(chart);
        built_timing = std::move(*result.index);
        cached_timing = &*built_timing;
    }
    const auto& clock = *cached_timing;
    const auto start_row = first < measures.entries.size()
                               ? measures.entries[first].absolute_begin_row
                               : measures.total_rows;
    const auto end_row = last < measures.entries.size() ? measures.entries[last].absolute_begin_row
                                                        : measures.total_rows;
    const auto window_begin = stem_seconds_at(clock, start_row);
    const auto window_end = stem_seconds_at(clock, end_row);
    if (!window_begin || !window_end)
        return all_stem_channels(chart);
    const auto chart_end = stem_seconds_at(clock, measures.total_rows).value_or(*window_end);
    std::vector<const sound_channel_t*> visible;
    for (const auto& channel : chart.sound_channels) {
        if (visible.size() >= maximum_visible_stems)
            break;
        const auto duration = stem_duration_seconds(document, channel, bindings);
        const auto inside =
            stem_channel_reaches_window(channel, index_of, measures, clock, first, last,
                                        *window_begin, *window_end, chart_end, duration);
        if (inside)
            visible.push_back(&channel);
    }
    return visible;
}

} // namespace wormedit::sys
