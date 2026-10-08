#pragma once

#include <wormedit/audio/source_registry.h>
#include "common/static_interval_index.h"
#include <wormedit/core/document.h>
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
struct packed_waveform_index_t {
    const song_document_t* document{};
    const chart_document_t* chart{};
    std::uint64_t revision{}, timing_fingerprint{};
    bool valid{};
    std::uint64_t builds{}, cache_hits{};
    mutable std::size_t queried_pieces{};
    std::vector<audio::decoded_source_binding_t> bindings;
    std::vector<packed_waveform_piece_t> pieces;
    common::static_interval_index_t intervals;
};

inline auto packed_seconds(const timing_index_t& timing, std::uint64_t row,
                           timing_boundary_side_t side = timing_boundary_side_t::event)
    -> std::optional<double> {
    const auto value = timing_at(timing, row, side);
    if (!value.seconds || !value.seconds->denominator) return std::nullopt;
    return static_cast<double>(value.seconds->numerator) / value.seconds->denominator;
}
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
    std::unordered_map<std::uint64_t, const resource_entry_t*> resources;
    std::unordered_map<std::uint64_t, const audio_asset_t*> assets;
    std::unordered_map<std::uint64_t, const audio::decoded_source_binding_t*> sources;
    std::unordered_map<std::uint64_t, const measure_index_entry_t*> measures;
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
        builder.resources.emplace(resource.id.value, &resource);
    for (const auto& asset : builder.document.audio_assets) builder.assets.emplace(asset.id.value, &asset);
    for (const auto& binding : builder.bindings) builder.sources.emplace(binding.asset.value, &binding);
    for (const auto& measure : measures.entries) builder.measures.emplace(measure.id.value, &measure);
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
        const auto found = builder.resources.find(event.sound->value);
        if (found == builder.resources.end()) return std::nullopt;
        asset = found->second->asset;
    } else if (channel) asset = channel->source_clip.source;
    if (!asset || !builder.assets.contains(asset.value)) return std::nullopt;
    if (channel && channel->source_clip.source != asset) return std::nullopt;
    return channel ? channel->source_clip : audio_clip_t{.source = asset};
}
inline void packed_source_geometry(const packed_geometry_builder_t& builder,
                                   packed_waveform_piece_t& piece) {
    const auto* asset = builder.assets.at(piece.clip.source.value);
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
    const auto measure = builder.measures.find(position.measure.value);
    if (measure == builder.measures.end() || position.row >= measure->second->row_capacity ||
        position.row > std::numeric_limits<std::uint64_t>::max() - measure->second->absolute_begin_row)
        return std::nullopt;
    return measure->second->absolute_begin_row + position.row;
}
inline auto make_packed_piece(packed_geometry_builder_t& builder, const bgm_event_t& event)
    -> std::optional<packed_waveform_piece_t> {
    const auto measure = builder.measures.find(event.position.measure.value);
    const auto clip = packed_event_clip(builder, event);
    if (!clip || measure == builder.measures.end() ||
        event.position.row >= measure->second->row_capacity ||
        event.position.row > std::numeric_limits<std::uint64_t>::max() - measure->second->absolute_begin_row)
        return std::nullopt;
    const auto row = measure->second->absolute_begin_row + event.position.row;
    const auto seconds = packed_seconds(builder.timing, row);
    const auto duration = packed_measure_seconds(builder.timing, *measure->second, row);
    if (!seconds || !duration || !std::isfinite(*seconds) || !std::isfinite(*duration)) return std::nullopt;
    packed_waveform_piece_t piece{.event = event.id, .lane = event.source_lane, .clip = *clip,
        .position = event.position, .row = row, .begin_seconds = *seconds,
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
            piece.end_seconds = packed_seconds(timing, next->second).value_or(piece.begin_seconds);
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
inline void prepare_packed_waveform_index(packed_waveform_index_t& index,
    const song_document_t& document, const chart_document_t& chart,
    std::span<const audio::decoded_source_binding_t> bindings, const timing_index_t& timing) {
    if (index.valid && index.document == &document && index.chart == &chart &&
        index.revision == document.revision && index.timing_fingerprint == timing.source_fingerprint &&
        index.bindings.size() == bindings.size() && std::equal(index.bindings.begin(), index.bindings.end(), bindings.begin())) {
        ++index.cache_hits; return;
    }
    const auto builds = index.builds + 1;
    index = {};
    index.builds = builds;
    index.document = &document; index.chart = &chart; index.revision = document.revision;
    index.timing_fingerprint = timing.source_fingerprint;
    index.bindings.assign(bindings.begin(), bindings.end());
    const auto measures = build_measure_index(chart);
    if (chart.source_family != format_family_t::bmson || !std::holds_alternative<measure_index_t>(measures)) return;
    packed_geometry_builder_t builder{document, chart, timing, bindings};
    prepare_packed_geometry(builder, std::get<measure_index_t>(measures));
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
    index.valid = true;
}
inline auto visible_packed_waveform_pieces(const packed_waveform_index_t& index,
    double begin_seconds, double end_seconds) -> std::vector<const packed_waveform_piece_t*> {
    std::vector<const packed_waveform_piece_t*> result;
    index.queried_pieces = 0;
    if (!index.valid || !std::isfinite(begin_seconds) || !std::isfinite(end_seconds)) return result;
    const auto queried = index.intervals.query(begin_seconds, end_seconds);
    index.queried_pieces = queried.scanned;
    for (const auto selected : queried.indices) {
        const auto& piece = index.pieces[selected];
        if (piece.begin_seconds < end_seconds && piece.end_seconds > begin_seconds) result.push_back(&piece);
    }
    return result;
}

inline auto packed_tail_seconds_per_row(const timing_index_t& timing) -> std::optional<double> {
    if (timing.rows.empty() || timing.divisions_per_standard_measure == 0) return std::nullopt;
    const auto bpm = timing.rows.back().effective_bpm;
    if (bpm.numerator <= 0 || bpm.denominator == 0) return std::nullopt;
    return 240.0 * static_cast<double>(bpm.denominator) / bpm.numerator /
           timing.divisions_per_standard_measure;
}
inline auto packed_display_seconds(const timing_index_t& timing, std::uint64_t row,
    timing_boundary_side_t side = timing_boundary_side_t::event) -> std::optional<double> {
    if (row <= timing.total_rows) return packed_seconds(timing, row, side);
    const auto end = packed_seconds(timing, timing.total_rows, timing_boundary_side_t::after_stops);
    const auto scale = packed_tail_seconds_per_row(timing);
    if (!end || !scale) return std::nullopt;
    const auto seconds = *end + static_cast<double>(row - timing.total_rows) * *scale;
    return std::isfinite(seconds) ? std::optional{seconds} : std::nullopt;
}
inline auto packed_window_seconds(const timing_index_t& timing, double scroll, double height, double zoom)
    -> std::optional<std::pair<double, double>> {
    if (!std::isfinite(scroll) || !std::isfinite(height) || !std::isfinite(zoom) ||
        zoom <= 0 || height <= 0 || height > 4096 || std::abs(scroll) > 1e12) return std::nullopt;
    const auto scale = static_cast<double>(timing.divisions_per_standard_measure) / zoom;
    const auto first = std::max(0.0, std::floor((scroll - 1) * scale));
    const auto last = std::max(0.0, std::ceil((scroll + height + 1) * scale));
    if (!std::isfinite(last) || last >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) return std::nullopt;
    const auto begin = packed_display_seconds(timing, static_cast<std::uint64_t>(first), timing_boundary_side_t::before_delays);
    const auto end = packed_display_seconds(timing, static_cast<std::uint64_t>(last), timing_boundary_side_t::after_stops);
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
    std::size_t chart_position{};
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
    index.channels.reserve(std::min(chart.bgm_source_lanes.size(), chart.sound_channels.size()));
    for (std::size_t i = 0; i < chart.sound_channels.size(); ++i) {
        const auto& channel = chart.sound_channels[i];
        const auto* lane = stem_lane_for_channel(chart, channel.id);
        if (!lane) continue;
        stem_index_channel_t entry{.id = channel.id.value, .source_lane = lane->id.value,
                                   .chart_position = i};
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
    if (!index.built) return all_stem_channels(chart);
    if (!index.selection_valid || index.cursor != cursor || index.window != window)
        update_stem_selection(index, chart, cursor, window);
    std::vector<const sound_channel_t*> result;
    result.reserve(index.selected.size());
    for (const auto i : index.selected) {
        if (i >= index.channels.size()) return all_stem_channels(chart);
        const auto& entry = index.channels[i];
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
