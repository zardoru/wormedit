#include <wormedit/core/document_index.h>
#pragma once

#include <wormedit/audio/source_registry.h>
#include "common/static_interval_index.h"
#include <wormedit/core/document.h>
#include <wormedit/core/command.h>
#include <wormedit/core/keyed_cache.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/sound_slice.h>
#include <unordered_set>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <iterator>
#include <optional>
#include <span>
#include <unordered_map>
#include <variant>
#include <vector>
#include <utility>

namespace wormedit::sys {

// Packed geometry is independent of stem classification and the retained stem index.
struct packed_waveform_piece_t {
    object_id_t event;
    object_id_t lane;
    audio_clip_t clip;
    local_position_t position;
    std::uint64_t row{};
    std::optional<std::uint64_t> next_row;
    std::uint32_t sample_rate{};
    std::optional<std::uint64_t> source_frames;
    double begin_seconds{}, end_seconds{}, measure_seconds{};
};
struct packed_waveform_query_t {
    const song_document_t* document;
    const chart_document_t* chart;
    std::uint64_t revision, timing_fingerprint;
    std::span<const audio::decoded_source_binding_t> bindings;
};
struct packed_waveform_key_t {
    const song_document_t* document{};
    const chart_document_t* chart{};
    std::uint64_t revision{}, timing_fingerprint{};
    std::vector<audio::decoded_source_binding_t> bindings;
    auto operator==(const packed_waveform_key_t&) const -> bool = default;
    auto operator==(const packed_waveform_query_t& query) const -> bool {
        return document == query.document && chart == query.chart && revision == query.revision &&
            timing_fingerprint == query.timing_fingerprint && std::ranges::equal(bindings, query.bindings);
    }
    static auto from(const packed_waveform_query_t& query) -> packed_waveform_key_t {
        return {query.document, query.chart, query.revision, query.timing_fingerprint,
                {query.bindings.begin(), query.bindings.end()}};
    }
    void assign(const packed_waveform_query_t& query) {
        document = query.document; chart = query.chart; revision = query.revision;
        timing_fingerprint = query.timing_fingerprint;
        bindings = std::vector<audio::decoded_source_binding_t>{query.bindings.begin(), query.bindings.end()};
    }
};
struct packed_waveform_data_t {
    std::uint64_t cache_hits{};
    mutable std::size_t queried_pieces{};
    std::vector<packed_waveform_piece_t> pieces;
    common::static_interval_index_t intervals;
    static auto from(const song_document_t& document, const chart_document_t& chart,
        std::span<const audio::decoded_source_binding_t> bindings, const timing_index_t& timing,
        const measure_index_t* measures) -> std::optional<packed_waveform_data_t>;
};
using packed_waveform_index_t = keyed_cache_t<packed_waveform_key_t, packed_waveform_data_t>;

inline auto packed_measure_seconds(const timing_index_t& timing,
    const measure_index_entry_t& measure, std::uint64_t row) -> std::optional<double> {
    const auto after = std::upper_bound(timing.rows.begin(), timing.rows.end(), row,
        [](auto value, const auto& state) { return value < state.absolute_row; });
    if (after == timing.rows.begin() || timing.divisions_per_standard_measure == 0)
        return std::nullopt;
    const auto bpm = std::prev(after)->effective_bpm;
    if (bpm.numerator <= 0 || bpm.denominator == 0) return std::nullopt;
    return 240.0 * static_cast<double>(measure.row_capacity) / timing.divisions_per_standard_measure *
           static_cast<double>(bpm.denominator) / static_cast<double>(bpm.numerator);
}
struct packed_geometry_builder_t {
    const song_document_t& document;
    const chart_document_t& chart;
    const timing_index_t& timing;
    std::span<const audio::decoded_source_binding_t> bindings;
    std::unordered_set<std::uint64_t> lanes;
    std::unordered_map<std::uint64_t, const sound_channel_t*> channels;
    document_id_index_t<resource_entry_t> resources;
    document_id_index_t<audio_asset_t> assets;
    std::unordered_map<std::uint64_t, const audio::decoded_source_binding_t*> sources;
    measure_lookup_t measures;
    std::unordered_map<std::uint64_t, sound_slice_result_t> slices;
    std::unordered_set<std::uint64_t> continuation_channels;
    std::unordered_map<std::uint64_t, const derived_sound_slice_t*> spans;
};
inline void prepare_packed_geometry(packed_geometry_builder_t& builder,
                                    const measure_index_t& measures) {
    for (const auto& lane : builder.chart.bgm_source_lanes)
        if (!lane.stem_channel) builder.lanes.insert(lane.id.value);
    for (const auto& channel : builder.chart.sound_channels)
        for (const auto& event : channel.events) {
            builder.channels.emplace(event.id.value, &channel);
            if (event.continuation) builder.continuation_channels.insert(channel.id.value);
        }
    for (const auto& resource : builder.document.resources)
        builder.resources.add(resource);
    for (const auto& asset : builder.document.audio_assets) builder.assets.add(asset);
    for (const auto& binding : builder.bindings) builder.sources.emplace(binding.asset.value, &binding);
    builder.measures = measure_lookup_t::from(measures);
}
inline auto packed_event_clip(const packed_geometry_builder_t& builder, const bgm_event_t& event)
    -> std::optional<audio_clip_t> {
    const sound_channel_t* channel{};
    if (event.sound_channel_event) {
        const auto found = builder.channels.find(event.sound_channel_event->value);
        if (found != builder.channels.end()) channel = found->second;
    }
    asset_id_t asset;
    if (event.sound) {
        const auto found = builder.resources.find(*event.sound);
        if (found == nullptr) return std::nullopt;
        asset = found->asset;
    } else if (channel) asset = channel->source_clip.source;
    if (!asset || !builder.assets.has_entry(asset)) return std::nullopt;
    if (channel && channel->source_clip.source != asset) return std::nullopt;
    return channel ? channel->source_clip : audio_clip_t{.source = asset};
}
inline void packed_source_geometry(const packed_geometry_builder_t& builder,
                                   packed_waveform_piece_t& piece) {
    const auto* asset = builder.assets.at(piece.clip.source);
    piece.sample_rate = asset->stream.sample_rate;
    piece.source_frames = asset->stream.frame_count;
    const auto source = builder.sources.find(piece.clip.source.value);
    if (source == builder.sources.end()) return;
    if (!piece.sample_rate) piece.sample_rate = source->second->source_sample_rate;
    if (!piece.source_frames) piece.source_frames = source->second->source_frame_count;
}
inline auto packed_linked_span(packed_geometry_builder_t& builder, const bgm_event_t& event,
                               packed_waveform_piece_t& piece) -> bool {
    if (!event.sound_channel_event || !piece.sample_rate) return true;
    const auto found = builder.channels.find(event.sound_channel_event->value);
    if (found == builder.channels.end()) return false;
    const auto& channel = *found->second;
    if (!builder.continuation_channels.contains(channel.id.value)) return true;
    auto [entry, inserted] = builder.slices.try_emplace(channel.id.value);
    if (inserted) {
        entry->second = derive_sound_channel_slices(builder.chart, channel,
            piece.sample_rate, piece.source_frames);
        if (entry->second) for (const auto& slice : entry->second.slices)
            for (const auto id : slice.events) builder.spans.emplace(id.value, &slice);
    }
    if (!entry->second) return false;
    const auto found_span = builder.spans.find(event.sound_channel_event->value);
    if (found_span == builder.spans.end()) return false;
    const auto* slice = found_span->second;
    if (slice->absolute_row != piece.row) return false;
    piece.clip.begin_frame = slice->source_begin_frame;
    piece.clip.end_frame = slice->source_end_frame;
    return true;
}
inline auto packed_event_row(const packed_geometry_builder_t& builder, local_position_t position)
    -> std::optional<std::uint64_t> {
    return builder.measures.row_of(position);
}
inline auto make_packed_piece(packed_geometry_builder_t& builder, const bgm_event_t& event)
    -> std::optional<packed_waveform_piece_t> {
    const auto* measure = builder.measures.entry_for(event.position.measure);
    const auto clip = packed_event_clip(builder, event);
    const auto row = builder.measures.row_of(event.position);
    if (!clip || !measure || !row)
        return std::nullopt;
    const auto seconds = builder.timing.seconds_at_approx(*row, chart_end_policy_t::reject);
    const auto duration = packed_measure_seconds(builder.timing, *measure, *row);
    if (!seconds || !duration || !std::isfinite(*seconds) || !std::isfinite(*duration)) return std::nullopt;
    packed_waveform_piece_t piece{.event = event.id, .lane = event.source_lane, .clip = *clip,
        .position = event.position, .row = *row, .begin_seconds = *seconds,
        .end_seconds = std::numeric_limits<double>::infinity(), .measure_seconds = *duration};
    packed_source_geometry(builder, piece);
    if (!packed_linked_span(builder, event, piece)) return std::nullopt;
    return piece;
}
inline auto packed_known_duration(const packed_waveform_piece_t& piece) -> std::optional<double> {
    auto end = piece.clip.end_frame;
    if (piece.source_frames) end = std::min(end.value_or(*piece.source_frames), *piece.source_frames);
    if (!piece.sample_rate || !end) return std::nullopt;
    if (*end <= piece.clip.begin_frame) return 0.0;
    return static_cast<double>(*end - piece.clip.begin_frame) / piece.sample_rate;
}
inline void finish_packed_pieces(std::vector<packed_waveform_piece_t>& pieces,
    const std::unordered_map<std::uint64_t, std::uint64_t>& next_rows,
    const timing_index_t& timing) {
    for (auto& piece : pieces) {
        const auto next = next_rows.find(piece.event.value);
        if (next != next_rows.end()) {
            piece.next_row = next->second;
            piece.end_seconds = timing.seconds_at_approx(next->second, chart_end_policy_t::reject).value_or(piece.begin_seconds);
        }
        const auto duration = packed_known_duration(piece);
        if (duration) piece.end_seconds = std::min(piece.end_seconds, piece.begin_seconds + *duration);
    }
    std::erase_if(pieces, [](const auto& piece) {
        const auto duration = packed_known_duration(piece);
        return (duration && *duration <= piece.measure_seconds) || piece.end_seconds <= piece.begin_seconds;
    });
}
inline void append_next_packed_rows(
    std::vector<std::pair<std::uint64_t, std::uint64_t>>& rows,
    std::unordered_map<std::uint64_t, std::uint64_t>& next_rows) {
    std::stable_sort(rows.begin(), rows.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    std::optional<std::uint64_t> next;
    for (std::size_t i = rows.size(); i > 0; --i) {
        if (i < rows.size() && rows[i].first > rows[i - 1].first) next = rows[i].first;
        if (next) next_rows.emplace(rows[i - 1].second, *next);
    }
}
inline auto packed_waveform_data_t::from(const song_document_t& document,
    const chart_document_t& chart, std::span<const audio::decoded_source_binding_t> bindings,
    const timing_index_t& timing, const measure_index_t* measures)
    -> std::optional<packed_waveform_data_t> {
    std::optional<measure_index_t> built_measures;
    if (!measures) {
        auto built = measure_index_t::from(chart);
        if (!built) return std::nullopt;
        built_measures = std::move(*built);
        measures = &*built_measures;
    }
    if (chart.source_family != format_family_t::bmson) return std::nullopt;
    packed_waveform_data_t index;
    packed_geometry_builder_t builder{document, chart, timing, bindings};
    prepare_packed_geometry(builder, *measures);
    std::unordered_map<std::uint64_t, std::vector<std::pair<std::uint64_t, std::uint64_t>>> column_rows;
    for (const auto& measure : chart.measures) for (const auto& event : measure.bgm_events) {
        if (!builder.lanes.contains(event.source_lane.value)) continue;
        const auto row = packed_event_row(builder, event.position);
        if (row) column_rows[event.source_lane.value].emplace_back(*row, event.id.value);
        if (auto piece = make_packed_piece(builder, event)) index.pieces.push_back(*piece);
    }
    std::unordered_map<std::uint64_t, std::uint64_t> next_rows;
    for (auto& [lane, rows] : column_rows) append_next_packed_rows(rows, next_rows);
    finish_packed_pieces(index.pieces, next_rows, timing);
    std::vector<common::static_interval_t> intervals;
    intervals.reserve(index.pieces.size());
    for (std::size_t i = 0; i < index.pieces.size(); ++i)
        intervals.push_back({index.pieces[i].begin_seconds,
            std::min(index.pieces[i].end_seconds, std::numeric_limits<double>::max()), i});
    index.intervals.rebuild(intervals);
    return index;
}

inline void prepare_packed_waveform_index(packed_waveform_index_t& index,
    const song_document_t& document, const chart_document_t& chart,
    std::span<const audio::decoded_source_binding_t> bindings, const timing_index_t& timing,
    const measure_index_t* measures = nullptr) {
    const auto previous_builds = index.build_count();
    bool succeeded = true;
    auto& data = index.get_or_build(
        packed_waveform_query_t{&document, &chart, document.revision, timing.source_fingerprint, bindings},
        [&document, &chart, bindings, &timing, measures, &succeeded](packed_waveform_data_t& retained) {
            retained = {};
            auto built = packed_waveform_data_t::from(document, chart, bindings, timing, measures);
            succeeded = built.has_value();
            retained = std::move(built).value_or(packed_waveform_data_t{});
        });
    if (!succeeded) index.invalidate_value(); // Preserve retry after a failed packed build.
    else if (index.build_count() == previous_builds) ++data.cache_hits;
}
inline auto visible_packed_waveform_pieces(const packed_waveform_index_t& index,
    double begin_seconds, double end_seconds) -> std::vector<const packed_waveform_piece_t*> {
    std::vector<const packed_waveform_piece_t*> result;
    index.value().queried_pieces = 0;
    if (!index.has_value() || !std::isfinite(begin_seconds) || !std::isfinite(end_seconds)) return result;
    const auto queried = index.value().intervals.query(begin_seconds, end_seconds);
    index.value().queried_pieces = queried.scanned;
    for (const auto selected : queried.indices) {
        const auto& piece = index.value().pieces[selected];
        if (piece.begin_seconds < end_seconds && piece.end_seconds > begin_seconds) result.push_back(&piece);
    }
    return result;
}

inline auto packed_window_seconds(const timing_index_t& timing, double scroll, double height, double zoom)
    -> std::optional<std::pair<double, double>> {
    if (!std::isfinite(scroll) || !std::isfinite(height) || !std::isfinite(zoom) ||
        zoom <= 0 || height <= 0 || height > 4096 || std::abs(scroll) > 1e12) return std::nullopt;
    const auto scale = static_cast<double>(timing.divisions_per_standard_measure) / zoom;
    const auto first = std::max(0.0, std::floor((scroll - 1) * scale));
    const auto last = std::max(0.0, std::ceil((scroll + height + 1) * scale));
    if (!std::isfinite(last) || last >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) return std::nullopt;
    const auto begin = timing.seconds_at_approx(static_cast<std::uint64_t>(first), chart_end_policy_t::extrapolate_tail, timing_boundary_side_t::before_delays);
    const auto end = timing.seconds_at_approx(static_cast<std::uint64_t>(last), chart_end_policy_t::extrapolate_tail, timing_boundary_side_t::after_stops);
    if (!begin || !end) return std::nullopt;
    return std::pair{*begin, *end};
}

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
[[nodiscard]] inline auto stem_lane_for_channel(const chart_document_t& chart, object_id_t channel)
    -> const bgm_source_lane_t* {
    if (chart.source_family != format_family_t::bmson) return nullptr;
    const auto found = std::ranges::find(chart.bgm_source_lanes, std::optional{channel},
                                       &bgm_source_lane_t::stem_channel);
    return found == chart.bgm_source_lanes.end() ? nullptr : &*found;
}

[[nodiscard]] inline auto all_stem_channels( const chart_document_t& chart) -> std::vector<const sound_channel_t*> {
    std::vector<const sound_channel_t*> result;
    for (const auto& channel : chart.sound_channels) {
        if (stem_lane_for_channel(chart, channel.id)) result.push_back(&channel);
    }
    return result;
}


[[nodiscard]] inline auto
stem_duration_seconds(const song_document_t& document, const sound_channel_t& channel,
                      std::span<const audio::decoded_source_binding_t> bindings)
    -> std::optional <
    double> {
    const auto asset = document.find_audio_asset(channel.source_clip.source);
    auto rate =asset == nullptr ? 0U : asset->stream.sample_rate;
    auto frames = asset == nullptr ? std::nullopt : asset->stream.frame_count;
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
            at == index_of.end() ? std::nullopt : (measures).row_of(event.position);
        const auto event_time = row ? timing.seconds_at_approx(*row, chart_end_policy_t::reject, timing_boundary_side_t::after_stops) : std::nullopt;
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
    std::size_t chart_position{};
    std::vector<stem_visibility_interval_t> intervals;
    std::vector<std::size_t> event_measures;
    bool conservative{};
};
struct stem_geometry_indices_t {
    const timing_index_t* timing{};
    const result_t<measure_index_t, diagnostic_list_t>* measures{};
};
struct stem_index_query_t {
    chart_stamp_t stamp;
    std::span<const audio::decoded_source_binding_t> bindings;
    const timing_index_t* timing;
    std::uint64_t& built_timing_fingerprint;
};
struct stem_index_key_t {
    chart_stamp_t stamp;
    std::uint64_t timing_fingerprint{};
    std::vector<audio::decoded_source_binding_t> bindings;
    auto operator==(const stem_index_key_t&) const -> bool = default;
    auto operator==(const stem_index_query_t& query) const -> bool {
        return stamp == query.stamp && std::ranges::equal(bindings, query.bindings) &&
               (!query.timing || timing_fingerprint == query.timing->source_fingerprint);
    }
    static auto from(const stem_index_query_t& query) -> stem_index_key_t {
        return {query.stamp, query.built_timing_fingerprint,
                {query.bindings.begin(), query.bindings.end()}};
    }
    void assign(const stem_index_query_t& query) {
        stamp = query.stamp;
        timing_fingerprint = query.built_timing_fingerprint;
        bindings = std::vector<audio::decoded_source_binding_t>{query.bindings.begin(), query.bindings.end()};
    }
};
struct stem_index_data_t {
    // Failed builds keep their partial products for the existing fallback contract.
    bool is_built{};
    measure_index_t measures;
    timing_index_t timing;
    std::unordered_map<std::uint64_t, std::size_t> measure_by_id;
    std::vector<stem_index_channel_t> channels;
    keyed_cache_t<std::pair<measure_id_t, std::size_t>, std::vector<std::size_t>> selection;
};
using stem_index_t = keyed_cache_t<stem_index_key_t, stem_index_data_t>;

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
    const sound_channel_event_t& event, const stem_index_data_t& index, stem_slice_scan_t& scan,
    std::optional<double> duration, double chart_end) -> bool {
    const auto at = index.measure_by_id.find(event.position.measure.value);
    if (at == index.measure_by_id.end()) return false;
    const auto row = (index.measures).row_of(event.position);
    const auto when = row ? index.timing.seconds_at_approx(*row, chart_end_policy_t::reject, timing_boundary_side_t::after_stops) : std::nullopt;
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
    const stem_index_data_t& index, double chart_end,
    std::span<const audio::decoded_source_binding_t> bindings) {
    const auto duration = stem_duration_seconds(document, channel, bindings);
    stem_slice_scan_t scan;
    for (const auto& event : channel.events) {
        if (!stem_index_add_event(result, event, index, scan, duration, chart_end)) {
            result.conservative = true;
            return;
        }
    }
    stem_finish_channel_intervals(result, scan, duration, chart_end);
}

inline void build_stem_index(stem_index_data_t& index, const song_document_t& document,
    const chart_document_t& chart, std::span<const audio::decoded_source_binding_t> bindings,
    stem_geometry_indices_t indices) {
    index = {};
    if (indices.measures) {
        if (!*indices.measures) return;
        index.measures = **indices.measures;
    } else {
        const auto built = measure_index_t::from(chart);
        if (!built) return;
        index.measures = *built;
    }
    index.measure_by_id.reserve(chart.measures.size());
    for (std::size_t i = 0; i < chart.measures.size(); ++i) index.measure_by_id.emplace(chart.measures[i].id.value, i);
    if (indices.timing) index.timing = *indices.timing;
    else {
        auto built = timing_index_t::from(chart, {.allow_bounded_clock = true});
        if (!built.value) return;
        index.timing = std::move(*built.value);
    }
    const auto chart_end = index.timing.seconds_at_approx(index.measures.total_rows, chart_end_policy_t::reject, timing_boundary_side_t::after_stops);
    if (!chart_end) return;
    index.is_built = true;
    index.channels.reserve(std::min(chart.bgm_source_lanes.size(), chart.sound_channels.size()));
    for (std::size_t i = 0; i < chart.sound_channels.size(); ++i) {
        const auto& channel = chart.sound_channels[i];
        const auto* lane = stem_lane_for_channel(chart, channel.id);
        if (!lane) continue;
        stem_index_channel_t entry{.id = channel.id.value, .source_lane = lane->id.value,
                                   .chart_position = i};
        stem_build_channel_intervals(entry, document, channel, index, *chart_end, bindings);
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

inline void update_stem_selection(stem_index_data_t& index, const chart_document_t& chart,
                                  measure_id_t cursor, std::size_t window,
                                  std::vector<std::size_t>& selected) {
    selected.clear();
    const auto found = index.measure_by_id.find(cursor.value);
    const auto center = found == index.measure_by_id.end() ? std::size_t{0} : found->second;
    const auto first = center > window ? center - window : 0;
    const auto last = std::min(chart.measures.size(), center + window + 1);
    const auto begin_row = first < index.measures.entries.size() ? index.measures.entries[first].absolute_begin_row : index.measures.total_rows;
    const auto end_row = last < index.measures.entries.size() ? index.measures.entries[last].absolute_begin_row : index.measures.total_rows;
    const auto begin = index.timing.seconds_at_approx(begin_row, chart_end_policy_t::reject, timing_boundary_side_t::after_stops);
    const auto end = index.timing.seconds_at_approx(end_row, chart_end_policy_t::reject, timing_boundary_side_t::after_stops);
    for (std::size_t i = 0; i < index.channels.size(); ++i)
        if (stem_channel_index_selected(index.channels[i], first, last, begin, end))
            selected.push_back(i);
}

[[nodiscard]] inline auto indexed_visible_stem_channels(stem_index_t& index,
    const song_document_t& document, const chart_document_t& chart, measure_id_t cursor,
    std::size_t window, std::span<const audio::decoded_source_binding_t> bindings,
    stem_geometry_indices_t indices = {}) -> std::vector<const sound_channel_t*> {
    if (indices.measures && !*indices.measures) return all_stem_channels(chart);
    std::uint64_t built_timing_fingerprint{};
    auto& data = index.get_or_build(
        stem_index_query_t{chart_stamp_t::from(document, chart.id), bindings, indices.timing,
                           built_timing_fingerprint},
        [&document, &chart, bindings, indices, &built_timing_fingerprint](stem_index_data_t& retained) {
            build_stem_index(retained, document, chart, bindings, indices);
            built_timing_fingerprint = retained.timing.source_fingerprint;
        }, [](const stem_index_data_t& retained) { return retained.is_built; });
    if (!data.is_built) return all_stem_channels(chart);
    const auto& selected = data.selection.get_or_build(std::pair{cursor, window},
        [&data, &chart, cursor, window](std::vector<std::size_t>& selected) {
            update_stem_selection(data, chart, cursor, window, selected);
        });
    std::vector<const sound_channel_t*> result;
    result.reserve(selected.size());
    for (const auto i : selected) {
        if (i >= data.channels.size()) return all_stem_channels(chart);
        const auto& entry = data.channels[i];
        if (entry.chart_position >= chart.sound_channels.size() ||
            chart.sound_channels[entry.chart_position].id.value != entry.id)
            return all_stem_channels(chart);
        result.push_back(&chart.sound_channels[entry.chart_position]);
    }
    return result;
}

// The sound channels that can be heard in the window, in chart order:
// those with an event inside it, or an earlier one whose audio is long
// enough to reach it. A cursor outside the chart selects the first measures.
[[nodiscard]] inline auto visible_stem_channels(
    const song_document_t& document, const chart_document_t& chart, measure_id_t cursor,
    std::size_t window_measures, std::span<const audio::decoded_source_binding_t> bindings = {},
    stem_geometry_indices_t indices = {}) -> std::vector<const sound_channel_t*> {
    std::unordered_map<std::uint64_t, std::size_t> index_of;
    index_of.reserve(chart.measures.size());
    for (std::size_t index = 0; index < chart.measures.size(); ++index)
        index_of.emplace(chart.measures[index].id.value, index);
    const auto found = index_of.find(cursor.value);
    const auto center = found == index_of.end() ? std::size_t{0} : found->second;
    const auto first = center > window_measures ? center - window_measures : 0;
    const auto last = std::min(chart.measures.size(), center + window_measures + 1);
    std::optional<measure_index_t> built_measures;
    if (indices.measures && !*indices.measures) return all_stem_channels(chart);
    const auto* measure_index = indices.measures ? &**indices.measures : nullptr;
    if (!measure_index) {
        auto built = measure_index_t::from(chart);
        if (!built) return all_stem_channels(chart);
        built_measures = std::move(*built);
        measure_index = &*built_measures;
    }
    const auto& measures = *measure_index;
    const auto* cached_timing = indices.timing;
    std::optional<timing_index_t> built_timing;
    if (!cached_timing) {
        auto result = timing_index_t::from(chart, {.allow_bounded_clock = true});
        if (!result.value)
            return all_stem_channels(chart);
        built_timing = std::move(*result.value);
        cached_timing = &*built_timing;
    }
    const auto& clock = *cached_timing;
    const auto start_row = first < measures.entries.size()
                               ? measures.entries[first].absolute_begin_row
                               : measures.total_rows;
    const auto end_row = last < measures.entries.size() ? measures.entries[last].absolute_begin_row
                                                        : measures.total_rows;
    const auto window_begin = clock.seconds_at_approx(start_row, chart_end_policy_t::reject, timing_boundary_side_t::after_stops);
    const auto window_end = clock.seconds_at_approx(end_row, chart_end_policy_t::reject, timing_boundary_side_t::after_stops);
    if (!window_begin || !window_end)
        return all_stem_channels(chart);
    const auto chart_end = clock.seconds_at_approx(measures.total_rows, chart_end_policy_t::reject, timing_boundary_side_t::after_stops).value_or(*window_end);
    std::vector<const sound_channel_t*> visible;
    for (const auto& channel : chart.sound_channels) {
        if (!stem_lane_for_channel(chart, channel.id)) continue;
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
