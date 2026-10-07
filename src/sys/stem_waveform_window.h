#pragma once

#include <wormedit/audio/source_registry.h>
#include <wormedit/core/document.h>
#include <wormedit/core/timing.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <unordered_map>
#include <variant>
#include <vector>

namespace wormedit::sys {

// A BMSON chart can have dozens of stems, but only the ones playing near the cursor are worth
// decoding and drawing. The window is measured in chart measures around the cursor's measure.

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

struct stem_visibility_interval_t { double begin{}, end{}; };
struct stem_index_channel_t {
    std::uint64_t id{}, source_lane{};
    std::vector<stem_visibility_interval_t> intervals;
    std::vector<std::size_t> event_measures;
    bool conservative{};
};
struct stem_index_t {
    song_id_t document;
    chart_id_t chart;
    std::uint64_t revision{};
    std::uint64_t timing_fingerprint{};
    bool built{};
    std::vector<audio::decoded_source_binding_t> bindings;
    measure_index_t measures;
    timing_index_t timing;
    std::unordered_map<std::uint64_t, std::size_t> measure_by_id;
    std::vector<stem_index_channel_t> channels;
    std::optional<measure_id_t> cursor;
    std::size_t window{};
    std::vector<std::size_t> selected;
    std::uint64_t selection_build_count{};
    bool selection_valid{};
};

[[nodiscard]] inline auto stem_index_current(const stem_index_t& index,
    const song_document_t& document, const chart_document_t& chart,
    std::span<const audio::decoded_source_binding_t> bindings) -> bool {
    return index.built && index.document == document.id && index.chart == chart.id && index.revision == document.revision &&
           index.bindings.size() == bindings.size() &&
           std::equal(index.bindings.begin(), index.bindings.end(), bindings.begin());
}

inline void stem_add_interval(stem_index_channel_t& channel, double begin, double end) {
    if (end > begin) channel.intervals.push_back({begin, end});
}

[[nodiscard]] inline auto stem_intervals_overlap(const std::vector<stem_visibility_interval_t>& intervals,
                                                  double begin, double end) -> bool {
    const auto found = std::lower_bound(intervals.begin(), intervals.end(), begin,
        [](const auto& interval, double value) { return interval.end <= value; });
    return found != intervals.end() && found->begin < end;
}

[[nodiscard]] inline auto stem_index_add_event(stem_index_channel_t& result,
    const sound_channel_event_t& event, const stem_index_t& index, stem_slice_scan_t& scan,
    std::optional<double> duration, double chart_end) -> bool {
    const auto at = index.measure_by_id.find(event.position.measure.value);
    if (at == index.measure_by_id.end()) return false;
    const auto row = absolute_row(index.measures, event.position);
    const auto when = row ? stem_seconds_at(index.timing, *row) : std::nullopt;
    if (!when || (scan.previous_time && *when < *scan.previous_time)) return false;
    if (scan.previous_time) {
        const auto end = duration ? std::min(*when, *scan.restart_time + *duration) : *when;
        stem_add_interval(result, *scan.previous_time, end);
    }
    if (!event.continuation || !scan.restart_time) scan.restart_time = *when;
    scan.previous_time = *when;
    if (!duration || *when < *scan.restart_time + *duration)
        result.event_measures.push_back(at->second);
    if (duration) stem_add_interval(result, *when, std::min(chart_end, *scan.restart_time + *duration));
    else stem_add_interval(result, *when, std::nextafter(*when, std::numeric_limits<double>::infinity()));
    return true;
}

inline void stem_finish_channel_intervals(stem_index_channel_t& result,
    const stem_slice_scan_t& scan, std::optional<double> duration, double chart_end) {
    if (scan.previous_time && scan.restart_time) {
        const auto end = duration ? std::min(chart_end, *scan.restart_time + *duration) : chart_end;
        stem_add_interval(result, *scan.previous_time, end);
    }
    std::ranges::sort(result.intervals, {}, &stem_visibility_interval_t::begin);
    std::vector<stem_visibility_interval_t> merged;
    for (const auto interval : result.intervals) {
        if (!merged.empty() && interval.begin <= merged.back().end)
            merged.back().end = std::max(merged.back().end, interval.end);
        else merged.push_back(interval);
    }
    result.intervals = std::move(merged);
    std::ranges::sort(result.event_measures);
    result.event_measures.erase(std::unique(result.event_measures.begin(), result.event_measures.end()),
                                result.event_measures.end());
}

inline void stem_build_channel_intervals(stem_index_channel_t& result,
    const song_document_t& document, const sound_channel_t& channel,
    const stem_index_t& index, double chart_end) {
    const auto duration = stem_duration_seconds(document, channel, index.bindings);
    stem_slice_scan_t scan;
    for (const auto& event : channel.events) {
        if (!stem_index_add_event(result, event, index, scan, duration, chart_end)) {
            result.conservative = true;
            return;
        }
    }
    stem_finish_channel_intervals(result, scan, duration, chart_end);
}

inline void build_stem_index(stem_index_t& index, const song_document_t& document,
    const chart_document_t& chart, std::span<const audio::decoded_source_binding_t> bindings,
    const timing_index_t* cached_timing) {
    index = {};
    index.document = document.id; index.chart = chart.id; index.revision = document.revision;
    index.bindings.assign(bindings.begin(), bindings.end());
    const auto measure_result = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(measure_result)) return;
    index.measures = std::get<measure_index_t>(measure_result);
    index.measure_by_id.reserve(chart.measures.size());
    for (std::size_t i = 0; i < chart.measures.size(); ++i) index.measure_by_id.emplace(chart.measures[i].id.value, i);
    if (cached_timing) index.timing = *cached_timing;
    else {
        auto built = build_timing_index(chart, {.allow_bounded_clock = true});
        if (!built) return;
        index.timing = std::move(*built.index);
    }
    index.timing_fingerprint = index.timing.source_fingerprint;
    const auto chart_end = stem_seconds_at(index.timing, index.measures.total_rows);
    if (!chart_end) return;
    index.built = true;
    index.channels.reserve(chart.sound_channels.size());
    std::unordered_map<std::uint64_t, std::pair<std::size_t, std::uint64_t>> lane_by_event;
    std::size_t encounter_rank{};
    for (const auto& measure : chart.measures) for (const auto& event : measure.bgm_events) {
        if (event.sound_channel_event)
            lane_by_event.try_emplace(event.sound_channel_event->value,
                std::pair{encounter_rank, event.source_lane.value});
        ++encounter_rank;
    }
    for (std::size_t i = 0; i < chart.sound_channels.size(); ++i) {
        const auto& channel = chart.sound_channels[i];
        stem_index_channel_t entry{.id = channel.id.value};
        const auto by_order = std::ranges::find(chart.bgm_source_lanes, i, &bgm_source_lane_t::source_order);
        if (by_order != chart.bgm_source_lanes.end()) entry.source_lane = by_order->id.value;
        else {
            auto earliest_rank = std::numeric_limits<std::size_t>::max();
            for (const auto& event : channel.events) {
                const auto lane = lane_by_event.find(event.id.value);
                if (lane != lane_by_event.end() && lane->second.first < earliest_rank) {
                    earliest_rank = lane->second.first;
                    entry.source_lane = lane->second.second;
                }
            }
        }
        stem_build_channel_intervals(entry, document, channel, index, *chart_end);
        index.channels.push_back(std::move(entry));
    }
}

[[nodiscard]] inline auto stem_channel_index_selected(const stem_index_channel_t& channel,
    std::size_t first, std::size_t last, std::optional<double> begin,
    std::optional<double> end) -> bool {
    const auto event = std::lower_bound(channel.event_measures.begin(), channel.event_measures.end(), first);
    const bool event_in_range = event != channel.event_measures.end() && *event < last;
    return channel.conservative || event_in_range ||
           (begin && end && stem_intervals_overlap(channel.intervals, *begin, *end));
}

inline void update_stem_selection(stem_index_t& index, const chart_document_t& chart,
                                  measure_id_t cursor, std::size_t window) {
    ++index.selection_build_count;
    index.selected.clear();
    const auto found = index.measure_by_id.find(cursor.value);
    const auto center = found == index.measure_by_id.end() ? std::size_t{0} : found->second;
    const auto first = center > window ? center - window : 0;
    const auto last = std::min(chart.measures.size(), center + window + 1);
    const auto begin_row = first < index.measures.entries.size() ? index.measures.entries[first].absolute_begin_row : index.measures.total_rows;
    const auto end_row = last < index.measures.entries.size() ? index.measures.entries[last].absolute_begin_row : index.measures.total_rows;
    const auto begin = stem_seconds_at(index.timing, begin_row);
    const auto end = stem_seconds_at(index.timing, end_row);
    for (std::size_t i = 0; i < index.channels.size(); ++i)
        if (stem_channel_index_selected(index.channels[i], first, last, begin, end))
            index.selected.push_back(i);
    index.cursor = cursor;
    index.window = window;
    index.selection_valid = true;
}

[[nodiscard]] inline auto indexed_visible_stem_channels(stem_index_t& index,
    const song_document_t& document, const chart_document_t& chart, measure_id_t cursor,
    std::size_t window, std::span<const audio::decoded_source_binding_t> bindings,
    const timing_index_t* timing = nullptr) -> std::vector<const sound_channel_t*> {
    if (!stem_index_current(index, document, chart, bindings) ||
        (timing && index.timing_fingerprint != timing->source_fingerprint))
        build_stem_index(index, document, chart, bindings, timing);
    if (index.channels.size() != chart.sound_channels.size()) return all_stem_channels(chart);
    if (!index.selection_valid || index.cursor != cursor || index.window != window)
        update_stem_selection(index, chart, cursor, window);
    std::vector<const sound_channel_t*> result;
    result.reserve(index.selected.size());
    for (const auto i : index.selected) if (i < chart.sound_channels.size()) result.push_back(&chart.sound_channels[i]);
    return result;
}

// The sound channels that can be heard in the window, in chart order:
// those with an event inside it, or an earlier one whose audio is long
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
