#include "timing_arithmetic.h"
#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/measure.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/sound_slice.h>
#include <wormedit/core/snap.h>

#include <algorithm>
#include <charconv>
#include <array>
#include <limits>
#include <numeric>
#include <ranges>
#include <unordered_set>
#include <unordered_map>

namespace wormedit {
namespace {


struct bmson_restart_target_t {
    std::size_t chart_offset;
    std::size_t channel_offset;
    local_position_t position;
};

auto resolve_bmson_restart_target(document_history_t& history,
                                   std::uint64_t expected_revision, chart_id_t chart,
                                   object_id_t sound_channel_event)
    -> std::variant<bmson_restart_target_t, document_command_result_t> {
    if (expected_revision != history.document.revision) {
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON restart edit has a stale document revision");
    }
    const auto found_chart = std::ranges::find(history.document.charts, chart,
                                                &chart_document_t::id);
    if (!chart || found_chart == history.document.charts.end() ||
        found_chart->source_family != format_family_t::bmson || !sound_channel_event) {
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON restart edit requires a known BMSON chart and source event");
    }
    const auto channel_index = std::ranges::find_if(found_chart->sound_channels,
        [&](const auto& channel) {
            return std::ranges::any_of(channel.events, [&](const auto& event) {
                return event.id == sound_channel_event;
            });
        });
    if (channel_index == found_chart->sound_channels.end()) {
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON source event is not in the selected chart");
    }
    const auto target = std::ranges::find(channel_index->events, sound_channel_event,
                                           &sound_channel_event_t::id);
    if (target == channel_index->events.end() || target->playable) {
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON restart edit requires a non-playable sound event");
    }
    return bmson_restart_target_t{
        static_cast<std::size_t>(found_chart - history.document.charts.begin()),
        static_cast<std::size_t>(channel_index - found_chart->sound_channels.begin()),
        target->position};
}

auto apply_bmson_restart_state(document_history_t& history,
                                const bmson_restart_target_t& target, bool restart,
                                const char* description) -> document_command_result_t {
    const auto& existing =
        history.document.charts[target.chart_offset].sound_channels[target.channel_offset].events;
    const auto needs_change = std::ranges::any_of(existing, [&](const auto& event) {
        return event.position == target.position && event.continuation != !restart;
    });
    if (!needs_change) return {.revision = history.document.revision};
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    auto& events = proposed.charts[target.chart_offset].sound_channels[target.channel_offset].events;
    for (auto& event : events) {
        if (event.position == target.position) event.continuation = !restart;
    }
    return transaction.commit(description);
}

} // namespace

namespace {
auto razor_grid_positions(const chart_document_t& chart, const measure_index_t& index,
    std::uint64_t first, std::uint64_t last, std::uint64_t denominator,
    std::size_t maximum_positions) -> std::optional<std::vector<local_position_t>> {
    if (snap_absolute_row(chart, index, first, denominator) != first ||
        snap_absolute_row(chart, index, last, denominator) != last) return std::nullopt;
    std::vector<local_position_t> positions;
    positions.reserve(maximum_positions);
    auto row = first;
    while (row != last) {
        if (positions.size() == maximum_positions) return std::nullopt;
        const auto next = adjacent_snap_canvas_row(chart, index, row, denominator,
                                                   index.total_rows, first < last);
        if (!next || (first < last ? *next > last : *next < last)) return std::nullopt;
        const auto position = position_at_absolute_row(index, *next);
        if (!position) return std::nullopt;
        positions.push_back(*position);
        row = *next;
    }
    return positions;
}
} // namespace

auto interpolate_bmson_razor_path(const chart_document_t& chart,
                                  local_position_t from, local_position_t to,
                                  std::uint64_t snap_denominator,
                                  std::size_t maximum_positions) -> bmson_razor_path_t {
    bmson_razor_path_t result;
    const auto reject_path = [&](diagnostic_code_t code, std::string message) {
        result.diagnostics.push_back({.severity = diagnostic_severity_t::error,
                                      .code = code, .message = std::move(message),
                                      .object = std::nullopt});
    };
    if (chart.source_family != format_family_t::bmson) {
        reject_path(diagnostic_code_t::invalid_id,
                    "BMSON razor path requires a BMSON chart");
        return result;
    }
    maximum_positions = std::min<std::size_t>(maximum_positions, 256);
    const auto built = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(built)) {
        reject_path(diagnostic_code_t::invalid_measure,
                    "BMSON razor path requires valid chart measure geometry");
        return result;
    }
    const auto& index = std::get<measure_index_t>(built);
    const auto first = absolute_row(index, from);
    const auto last = absolute_row(index, to);
    if (!first || !last) {
        reject_path(diagnostic_code_t::row_out_of_range,
                    "BMSON razor path endpoint is outside exact chart geometry");
        return result;
    }
    if (chart.divisions_per_standard_measure == 0 ||
        (snap_denominator != 0 &&
         chart.divisions_per_standard_measure % snap_denominator != 0)) {
        reject_path(diagnostic_code_t::invalid_resolution,
                    "BMSON razor path has an unsupported snap denominator");
        return result;
    }
    const auto denominator = snap_denominator == 0
        ? chart.divisions_per_standard_measure : snap_denominator;
    auto positions = razor_grid_positions(chart, index, *first, *last, denominator,
                                                maximum_positions);
    if (!positions) {
        reject_path(diagnostic_code_t::invalid_resolution,
                    "BMSON razor path exceeds its grid or marker capacity");
        return result;
    }
    result.positions = std::move(*positions);
    return result;
}

auto toggle_bmson_sound_channel_restart(document_history_t& history,
                                        std::uint64_t expected_revision, chart_id_t chart,
                                        object_id_t sound_channel_event)
    -> document_command_result_t {
    const auto resolved =
        resolve_bmson_restart_target(history, expected_revision, chart, sound_channel_event);
    if (const auto* failure = std::get_if<document_command_result_t>(&resolved)) return *failure;
    const auto& target = std::get<bmson_restart_target_t>(resolved);
    const auto& events =
        history.document.charts[target.chart_offset].sound_channels[target.channel_offset].events;
    const auto has_restart = std::ranges::any_of(events, [&](const auto& event) {
        return event.position == target.position && !event.continuation;
    });
    return apply_bmson_restart_state(history, target, !has_restart,
                                     "Toggle BMSON sound-channel restart");
}

auto set_bmson_sound_channel_restart(document_history_t& history,
                                     std::uint64_t expected_revision, chart_id_t chart,
                                     object_id_t sound_channel_event, bool restart)
    -> document_command_result_t {
    const auto resolved =
        resolve_bmson_restart_target(history, expected_revision, chart, sound_channel_event);
    if (const auto* failure = std::get_if<document_command_result_t>(&resolved)) return *failure;
    return apply_bmson_restart_state(history, std::get<bmson_restart_target_t>(resolved), restart,
        restart ? "Set BMSON sound-channel restart" : "Set BMSON sound-channel continuation");
}

namespace {

auto translate_hold_end(const measure_index_t& index, local_position_t start,
                        std::optional<local_position_t> end, local_position_t target,
                        std::uint64_t revision)
    -> std::variant<std::optional<local_position_t>, document_command_result_t> {
    if (!end) return std::optional<local_position_t>{};
    const auto start_row = absolute_row(index, start);
    const auto end_row = absolute_row(index, *end);
    const auto target_row = absolute_row(index, target);
    if (!start_row || !end_row || !target_row || *end_row <= *start_row)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::row_out_of_range,
                        "BMSON marker hold has invalid span geometry");
    const auto length = *end_row - *start_row;
    if (length > std::numeric_limits<std::uint64_t>::max() - *target_row)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::row_out_of_range,
                        "BMSON marker hold exceeds exact chart geometry");
    const auto translated = position_at_absolute_row(index, *target_row + length);
    if (!translated)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::row_out_of_range,
                        "BMSON marker hold cannot be translated within chart geometry");
    return translated;
}

} // namespace

auto move_bmson_bgm_marker(document_history_t& history, std::uint64_t expected_revision,
                           chart_id_t chart, object_id_t bgm_event, local_position_t target)
    -> document_command_result_t {
    if (expected_revision != history.document.revision) {
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON marker move has a stale document revision");
    }
    const auto found_chart = std::ranges::find(history.document.charts, chart,
                                                &chart_document_t::id);
    if (!chart || found_chart == history.document.charts.end() ||
        found_chart->source_family != format_family_t::bmson || !bgm_event) {
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON marker move requires a known chart and BGM event");
    }
    const auto measures = build_measure_index(*found_chart);
    if (!std::holds_alternative<measure_index_t>(measures) ||
        !absolute_row(std::get<measure_index_t>(measures), target)) {
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::row_out_of_range,
                        "BMSON marker target is outside exact chart geometry");
    }
    const bgm_event_t* semantic{};
    for (const auto& measure : found_chart->measures) {
        const auto item = std::ranges::find(measure.bgm_events, bgm_event, &bgm_event_t::id);
        if (item != measure.bgm_events.end()) {
            semantic = &*item;
            break;
        }
    }
    if (!semantic || !semantic->sound_channel_event) {
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BGM marker has no retained BMSON sound-channel event");
    }
    if (semantic->position == target) {
        return {.revision = history.document.revision, .diagnostics = {}};
    }
    const auto channel = std::ranges::find_if(found_chart->sound_channels,
        [&](const auto& candidate) {
            return std::ranges::any_of(candidate.events, [&](const auto& event) {
                return event.id == *semantic->sound_channel_event;
            });
        });
    if (channel == found_chart->sound_channels.end()) {
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BGM marker source event is unavailable");
    }
    const auto source = std::ranges::find(channel->events, *semantic->sound_channel_event,
                                           &sound_channel_event_t::id);
    if (source == channel->events.end() || source->playable ||
        source->position != semantic->position) {
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BGM marker and retained source event disagree");
    }
    const auto translated = translate_hold_end(std::get<measure_index_t>(measures),
        semantic->position, semantic->end, target, history.document.revision);
    if (const auto* failure = std::get_if<document_command_result_t>(&translated)) return *failure;
    const auto translated_end = std::get<std::optional<local_position_t>>(translated);
    if (source->end != semantic->end)
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BGM marker and retained source hold ends disagree");
    if (std::ranges::any_of(channel->events, [&](const auto& event) {
            return event.id != source->id && event.position == target;
        })) {
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::collision,
                        "Another event in this BMSON sound channel occupies the target row");
    }
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    const auto chart_offset = static_cast<std::size_t>(found_chart - history.document.charts.begin());
    auto& changed_chart = proposed.charts[chart_offset];
    const auto channel_offset = static_cast<std::size_t>(channel - found_chart->sound_channels.begin());
    const auto source_offset = static_cast<std::size_t>(source - channel->events.begin());
    changed_chart.sound_channels[channel_offset].events[source_offset].position = target;
    changed_chart.sound_channels[channel_offset].events[source_offset].end = translated_end;
    for (auto& measure : changed_chart.measures) {
        std::erase_if(measure.bgm_events, [bgm_event](const auto& event) {
            return event.id == bgm_event;
        });
    }
    const auto destination = std::ranges::find(changed_chart.measures, target.measure,
                                                &measure_chunk_t::id);
    if (destination == changed_chart.measures.end()) {
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_measure,
                        "BMSON marker destination measure is unavailable");
    }
    auto moved = *semantic;
    moved.position = target;
    moved.end = translated_end;
    destination->bgm_events.push_back(std::move(moved));
    const auto asset = std::ranges::find(proposed.audio_assets,
        changed_chart.sound_channels[channel_offset].source_clip.source,
        &audio_asset_t::id);
    if (asset != proposed.audio_assets.end() && asset->stream.sample_rate != 0) {
        const auto slices = derive_sound_channel_slices(
            changed_chart, changed_chart.sound_channels[channel_offset],
            asset->stream.sample_rate, asset->stream.frame_count);
        if (!slices) {
            return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_resolution,
                            slices.diagnostics.front().message);
        }
    }
    return transaction.commit("Move BMSON BGM marker");
}

auto insert_bmson_bgm_marker(document_history_t& history, std::uint64_t expected_revision,
                             chart_id_t chart, object_id_t reference_bgm_event,
                             local_position_t target, bool restart)
    -> document_command_result_t {
    const std::array markers{bmson_sound_marker_insertion_t{target, restart}};
    return insert_bmson_sound_markers(history, expected_revision, chart, reference_bgm_event,
                                      markers);
}

namespace {

using lane_intervals_t =
    std::unordered_map<std::uint64_t, std::vector<std::pair<std::uint64_t, std::uint64_t>>>;

struct marker_source_t {
    const bgm_event_t* marker{};
    const sound_channel_t* channel{};
    const sound_channel_event_t* source{};
};

auto find_bgm_marker(const chart_document_t& chart, object_id_t id) -> const bgm_event_t* {
    for (const auto& measure : chart.measures) {
        const auto found = std::ranges::find(measure.bgm_events, id, &bgm_event_t::id);
        if (found != measure.bgm_events.end()) return &*found;
    }
    return nullptr;
}

auto resolve_marker_source(const chart_document_t& chart, object_id_t id,
                           std::uint64_t revision)
    -> std::variant<marker_source_t, document_command_result_t> {
    const auto* marker = find_bgm_marker(chart, id);
    if (!marker || !marker->sound_channel_event || !marker->sound)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id,
                       "Reference BGM marker has no retained BMSON sound event");
    const auto channel = std::ranges::find_if(chart.sound_channels, [&](const auto& candidate) {
        return std::ranges::any_of(candidate.events, [&](const auto& event) {
            return event.id == *marker->sound_channel_event;
        });
    });
    if (channel == chart.sound_channels.end())
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id,
                       "Reference BGM source event is unavailable");
    const auto source = std::ranges::find(channel->events, *marker->sound_channel_event,
                                          &sound_channel_event_t::id);
    if (source == channel->events.end() || source->playable || source->position != marker->position)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id,
                       "Reference BGM marker and retained source event disagree");
    return marker_source_t{marker, &*channel, &*source};
}

auto validate_marker_batch_geometry(const measure_index_t& index,
                                    std::span<const bmson_sound_marker_insertion_t> markers,
                                    std::uint64_t revision)
    -> std::optional<document_command_result_t> {
    for (std::size_t marker = 0; marker < markers.size(); ++marker) {
        if (!absolute_row(index, markers[marker].position))
            return document_command_transaction_t::rejection(revision, diagnostic_code_t::row_out_of_range,
                            "BMSON marker target is outside exact chart geometry");
        if (std::ranges::any_of(markers.first(marker), [&](const auto& prior) {
                return prior.position == markers[marker].position;
            }))
            return document_command_transaction_t::rejection(revision, diagnostic_code_t::collision,
                            "BMSON marker batch contains the same channel pulse twice");
    }
    return std::nullopt;
}

auto occupied_marker_lanes(const chart_document_t& chart, const measure_index_t& index,
                           std::span<const bmson_sound_marker_insertion_t> markers,
                           std::uint64_t revision)
    -> std::variant<lane_intervals_t, document_command_result_t> {
    lane_intervals_t occupied;
    for (const auto& marker : markers)
        if (marker.playable_lane) occupied.try_emplace(marker.playable_lane->value);
    if (occupied.empty()) return occupied;
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            const auto lane = occupied.find(note.lane.value);
            if (lane == occupied.end()) continue;
            const auto begin = absolute_row(index, note.position);
            const auto end = note.end ? absolute_row(index, *note.end) : begin;
            if (!begin || !end)
                return document_command_transaction_t::rejection(revision, diagnostic_code_t::row_out_of_range,
                                "BMSON target lane contains an invalid note position");
            lane->second.emplace_back(*begin, *end);
        }
    }
    for (auto& [id, intervals] : occupied) {
        (void)id;
        std::ranges::sort(intervals, {}, &std::pair<std::uint64_t, std::uint64_t>::first);
        std::uint64_t maximum_end{};
        for (auto& interval : intervals) {
            maximum_end = std::max(maximum_end, interval.second);
            interval.second = maximum_end;
        }
    }
    return occupied;
}

auto layout_at_row(const chart_document_t& chart, const measure_index_t& index,
                   std::uint64_t row) -> const lane_layout_t* {
    const lane_layout_t* selected{};
    std::uint64_t selected_row{};
    for (const auto& segment : chart.lane_layouts) {
        const auto begin = absolute_row(index, segment.position);
        if (begin && *begin <= row && (!selected || *begin >= selected_row)) {
            selected = &segment.layout;
            selected_row = *begin;
        }
    }
    return selected;
}

auto editable_lane_at(const chart_document_t& chart, const measure_index_t& index,
                      lane_id_t lane, std::uint64_t row) -> bool {
    const auto* layout = layout_at_row(chart, index, row);
    return layout && std::ranges::any_of(layout->lanes, [&](const auto& candidate) {
        return candidate.id == lane && candidate.role != lane_role_t::autoplay &&
               candidate.role != lane_role_t::invisible && candidate.role != lane_role_t::mine;
    });
}

auto validate_marker_targets(const chart_document_t& chart, const measure_index_t& index,
                             const sound_channel_t& channel,
                             std::span<const bmson_sound_marker_insertion_t> markers,
                             const lane_intervals_t& occupied, std::uint64_t revision)
    -> std::optional<document_command_result_t> {
    for (const auto& marker : markers) {
        if (std::ranges::any_of(channel.events, [&](const auto& event) {
                return event.position == marker.position;
            }))
            return document_command_transaction_t::rejection(revision, diagnostic_code_t::collision,
                            "Another event in this BMSON sound channel occupies the target row");
        if (!marker.playable_lane) continue;
        const auto row = absolute_row(index, marker.position);
        if (!*marker.playable_lane || !row)
            return document_command_transaction_t::rejection(revision, diagnostic_code_t::unknown_lane,
                            "BMSON marker has an invalid playable lane");
        if (!editable_lane_at(chart, index, *marker.playable_lane, *row))
            return document_command_transaction_t::rejection(revision, diagnostic_code_t::unknown_lane,
                            "BMSON marker playable lane is unavailable at its row");
        const auto& intervals = occupied.at(marker.playable_lane->value);
        const auto after = std::upper_bound(intervals.begin(), intervals.end(), *row,
            [](std::uint64_t value, const auto& interval) { return value < interval.first; });
        if (after != intervals.begin() && std::prev(after)->second >= *row)
            return document_command_transaction_t::rejection(revision, diagnostic_code_t::collision,
                            "A note occupies this staged BMSON playable lane");
    }
    return std::nullopt;
}

struct chart_object_stats_t {
    std::uint64_t largest{};
    std::size_t count{};
};

auto chart_object_stats(const chart_document_t& chart) -> chart_object_stats_t {
    chart_object_stats_t stats;
    const auto scan = [&](const auto& values) {
        for (const auto& value : values) {
            stats.largest = std::max(stats.largest, value.id.value);
            ++stats.count;
        }
    };
    scan(chart.bgm_source_lanes); scan(chart.lane_layouts); scan(chart.bpms);
    scan(chart.pauses); scan(chart.warps); scan(chart.scrolls); scan(chart.speeds);
    scan(chart.kiai); scan(chart.timing_anchors); scan(chart.bookmarks); scan(chart.sections);
    scan(chart.bga_events); scan(chart.sound_channels);
    for (const auto& channel : chart.sound_channels) {
        stats.largest = std::max(stats.largest, channel.source_clip.id.value);
        ++stats.count;
        scan(channel.events);
    }
    for (const auto& measure : chart.measures) {
        scan(measure.notes); scan(measure.bgm_events);
    }
    return stats;
}

auto append_marker(chart_document_t& chart, sound_channel_t& channel,
                   const bmson_sound_marker_insertion_t& marker,
                   const bgm_event_t& reference, std::uint64_t& largest,
                   std::uint64_t revision) -> std::optional<document_command_result_t> {
    const object_id_t source_id{++largest};
    const object_id_t semantic_id{++largest};
    channel.events.push_back({.id = source_id, .position = marker.position,
        .end = std::nullopt, .lane = marker.playable_lane.value_or(lane_id_t{}),
        .continuation = !marker.restart, .playable = marker.playable_lane.has_value(),
        .provenance = {}});
    const auto destination = std::ranges::find(chart.measures, marker.position.measure,
                                                &measure_chunk_t::id);
    if (destination == chart.measures.end())
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_measure,
                        "BMSON marker destination measure is unavailable");
    if (marker.playable_lane) {
        destination->notes.push_back({.id = semantic_id, .position = marker.position,
            .lane = *marker.playable_lane, .kind = note_kind_t::tap, .end = std::nullopt,
            .sound = reference.sound, .sound_channel_event = source_id, .provenance = {}});
    } else {
        destination->bgm_events.push_back({.id = semantic_id, .position = marker.position,
            .source_lane = reference.source_lane, .sound = reference.sound,
            .sound_channel_event = source_id, .structural_slot = 0, .branch = std::nullopt,
            .provenance = {}});
    }
    return std::nullopt;
}

auto validate_channel_slices(const song_document_t& document, const chart_document_t& chart,
                             const sound_channel_t& channel, std::uint64_t revision)
    -> std::optional<document_command_result_t> {
    const auto asset = std::ranges::find(document.audio_assets, channel.source_clip.source,
                                         &audio_asset_t::id);
    if (asset == document.audio_assets.end() || asset->stream.sample_rate == 0) return std::nullopt;
    const auto slices = derive_sound_channel_slices(chart, channel, asset->stream.sample_rate,
                                                    asset->stream.frame_count);
    if (slices) return std::nullopt;
    return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_resolution,
                    slices.diagnostics.front().message);
}

} // namespace

auto insert_bmson_sound_markers(document_history_t& history, std::uint64_t expected_revision,
                                chart_id_t chart, object_id_t reference_bgm_event,
                                std::span<const bmson_sound_marker_insertion_t> markers)
    -> document_command_result_t {
    const auto revision = history.document.revision;
    if (expected_revision != revision)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id,
                        "BMSON marker insertion has a stale document revision");
    if (markers.empty() || markers.size() > 256)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id,
                        "BMSON marker insertion requires 1 to 256 markers");
    const auto found_chart = std::ranges::find(history.document.charts, chart,
                                                &chart_document_t::id);
    if (!chart || found_chart == history.document.charts.end() ||
        found_chart->source_family != format_family_t::bmson || !reference_bgm_event)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id,
                        "BMSON marker insertion requires a known chart and BGM event");
    const auto built = build_measure_index(*found_chart);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built))
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_measure,
                        "BMSON chart has invalid measure geometry");
    const auto& index = std::get<measure_index_t>(built);
    if (auto failure = validate_marker_batch_geometry(index, markers, revision)) return *failure;
    auto source_result = resolve_marker_source(*found_chart, reference_bgm_event, revision);
    if (const auto* failure = std::get_if<document_command_result_t>(&source_result))
        return *failure;
    const auto source = std::get<marker_source_t>(source_result);
    auto occupied_result = occupied_marker_lanes(*found_chart, index, markers, revision);
    if (const auto* failure = std::get_if<document_command_result_t>(&occupied_result))
        return *failure;
    const auto& occupied = std::get<lane_intervals_t>(occupied_result);
    if (auto failure = validate_marker_targets(*found_chart, index, *source.channel,
                                                markers, occupied, revision)) return *failure;
    auto stats = chart_object_stats(*found_chart);
    const auto needed = markers.size() * 2;
    if (stats.count > default_maximum_chart_objects - needed ||
        stats.largest > std::numeric_limits<std::uint64_t>::max() - needed)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id,
                        "BMSON chart object ID space is exhausted");

    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    const auto chart_offset = static_cast<std::size_t>(found_chart - history.document.charts.begin());
    auto& changed_chart = proposed.charts[chart_offset];
    const auto channel_offset = static_cast<std::size_t>(
        source.channel - found_chart->sound_channels.data());
    auto& changed_channel = changed_chart.sound_channels[channel_offset];
    auto largest = stats.largest;
    for (const auto& marker : markers) {
        if (auto failure = append_marker(changed_chart, changed_channel, marker,
                                         *source.marker, largest, revision)) return *failure;
    }
    if (auto failure = validate_channel_slices(proposed, changed_chart, changed_channel, revision))
        return *failure;
    return transaction.commit(markers.size() == 1 ? "Insert BMSON sound marker" : "Insert BMSON sound markers");
}

auto delete_bmson_bgm_marker(document_history_t& history,
                             std::uint64_t expected_revision, chart_id_t chart,
                             object_id_t bgm_event) -> document_command_result_t {
    if (expected_revision != history.document.revision) {
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON marker deletion has a stale document revision");
    }
    const auto found_chart = std::ranges::find(history.document.charts, chart,
                                                &chart_document_t::id);
    if (!chart || found_chart == history.document.charts.end() ||
        found_chart->source_family != format_family_t::bmson || !bgm_event) {
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON marker deletion requires a known chart and BGM event");
    }
    const bgm_event_t* semantic{};
    for (const auto& measure : found_chart->measures) {
        const auto item = std::ranges::find(measure.bgm_events, bgm_event, &bgm_event_t::id);
        if (item != measure.bgm_events.end()) {
            semantic = &*item;
            break;
        }
    }
    if (!semantic || !semantic->sound_channel_event) {
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BGM marker has no retained BMSON sound-channel event");
    }
    const auto channel = std::ranges::find_if(found_chart->sound_channels,
        [&](const auto& candidate) {
            return std::ranges::any_of(candidate.events, [&](const auto& event) {
                return event.id == *semantic->sound_channel_event;
            });
        });
    if (channel == found_chart->sound_channels.end()) {
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BGM marker source event is unavailable");
    }
    const auto source = std::ranges::find(channel->events, *semantic->sound_channel_event,
                                           &sound_channel_event_t::id);
    if (source == channel->events.end() || source->playable ||
        source->position != semantic->position) {
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BGM marker and retained source event disagree");
    }
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    const auto chart_offset = static_cast<std::size_t>(found_chart - history.document.charts.begin());
    auto& changed_chart = proposed.charts[chart_offset];
    const auto channel_offset = static_cast<std::size_t>(channel - found_chart->sound_channels.begin());
    std::erase_if(changed_chart.sound_channels[channel_offset].events,
                  [id = *semantic->sound_channel_event](const auto& event) {
                      return event.id == id;
                  });
    for (auto& measure : changed_chart.measures) {
        std::erase_if(measure.bgm_events, [bgm_event](const auto& event) {
            return event.id == bgm_event;
        });
    }
    const auto asset = std::ranges::find(proposed.audio_assets,
        changed_chart.sound_channels[channel_offset].source_clip.source,
        &audio_asset_t::id);
    if (asset != proposed.audio_assets.end() && asset->stream.sample_rate != 0) {
        const auto slices = derive_sound_channel_slices(
            changed_chart, changed_chart.sound_channels[channel_offset],
            asset->stream.sample_rate, asset->stream.frame_count);
        if (!slices) {
            return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_resolution,
                            slices.diagnostics.front().message);
        }
    }
    return transaction.commit("Delete BMSON BGM marker");
}

namespace {

struct semantic_event_refs_t {
    const note_t* note{};
    const bgm_event_t* bgm{};
};

struct linked_sound_source_t {
    const sound_channel_t* channel{};
    const sound_channel_event_t* event{};
};

auto find_semantic_event(const chart_document_t& chart, object_id_t id)
    -> semantic_event_refs_t {
    semantic_event_refs_t result;
    for (const auto& measure : chart.measures) {
        if (const auto note = std::ranges::find(measure.notes, id, &note_t::id);
            note != measure.notes.end()) result.note = &*note;
        if (const auto bgm = std::ranges::find(measure.bgm_events, id, &bgm_event_t::id);
            bgm != measure.bgm_events.end()) result.bgm = &*bgm;
    }
    return result;
}

auto valid_semantic_event(const semantic_event_refs_t& refs) -> bool {
    return !((refs.note == nullptr) == (refs.bgm == nullptr)) &&
        (!refs.note || (((refs.note->kind == note_kind_t::tap && !refs.note->end) ||
                         (refs.note->kind == note_kind_t::hold && refs.note->end)) &&
                        !refs.note->label && !refs.note->branch &&
                        refs.note->sound_channel_event)) &&
        (!refs.bgm || (!refs.bgm->branch && refs.bgm->sound_channel_event));
}

auto assignment_span_end(const measure_index_t& index, std::uint64_t begin,
                         std::optional<local_position_t> end, std::uint64_t revision)
    -> std::variant<std::uint64_t, document_command_result_t> {
    if (!end) return begin;
    const auto finish = absolute_row(index, *end);
    if (!finish || *finish <= begin)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::row_out_of_range,
                        "BMSON linked hold requires a positive in-chart span");
    return *finish;
}

auto assignment_lane_occupancy(const chart_document_t& chart, const measure_index_t& index,
                               const semantic_event_refs_t& refs, lane_id_t lane,
                               std::uint64_t begin, std::uint64_t finish, bool hold,
                               std::uint64_t revision)
    -> std::optional<document_command_result_t> {
    const auto id = refs.note ? refs.note->id : refs.bgm->id;
    for (const auto& measure : chart.measures) {
        for (const auto& other : measure.notes) {
            if (other.id == id || other.lane != lane) continue;
            const auto other_begin = absolute_row(index, other.position);
            const auto other_end = other.end ? absolute_row(index, *other.end) : other_begin;
            if (!other_begin || !other_end || *other_end < *other_begin)
                return document_command_transaction_t::rejection(revision, diagnostic_code_t::row_out_of_range,
                                "Existing note has invalid BMSON lane geometry");
            const auto overlaps = hold
                ? *other_begin <= finish && begin <= *other_end
                : *other_begin <= begin && begin <= *other_end;
            if (overlaps)
                return document_command_transaction_t::rejection(revision, diagnostic_code_t::collision,
                                "Another note occupies the target BMSON playable lane");
        }
    }
    return std::nullopt;
}

auto assignment_lane_covers_hold(const chart_document_t& chart, const measure_index_t& index,
                                 lane_id_t lane, std::uint64_t begin, std::uint64_t finish,
                                 std::uint64_t revision)
    -> std::optional<document_command_result_t> {
    for (const auto& segment : chart.lane_layouts) {
        const auto transition = absolute_row(index, segment.position);
        if (transition && begin < *transition && *transition <= finish &&
            !editable_lane_at(chart, index, lane, *transition))
            return document_command_transaction_t::rejection(revision, diagnostic_code_t::unknown_lane,
                            "Target lane is unavailable within the BMSON hold span");
    }
    if (!editable_lane_at(chart, index, lane, finish))
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::unknown_lane,
                        "Target lane is unavailable at the BMSON hold end");
    return std::nullopt;
}

auto target_assignment_lane(const chart_document_t& chart, const measure_index_t& index,
                            const semantic_event_refs_t& refs,
                            std::optional<lane_id_t> lane, std::uint64_t revision)
    -> std::optional<document_command_result_t> {
    if (!lane) return std::nullopt;
    const auto position = refs.note ? refs.note->position : refs.bgm->position;
    const auto row = absolute_row(index, position);
    if (!row)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::row_out_of_range,
                        "BMSON sound assignment target is outside chart geometry");
    if (!editable_lane_at(chart, index, *lane, *row))
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::unknown_lane,
                        "Target playable lane is unavailable at the BMSON marker");
    const auto semantic_end = refs.note ? refs.note->end : refs.bgm->end;
    const auto end = assignment_span_end(index, *row, semantic_end, revision);
    if (const auto* failure = std::get_if<document_command_result_t>(&end)) return *failure;
    const auto finish = std::get<std::uint64_t>(end);
    if (auto failure = assignment_lane_occupancy(chart, index, refs, *lane, *row, finish,
                                                  semantic_end.has_value(), revision))
        return *failure;
    if (semantic_end)
        if (auto failure = assignment_lane_covers_hold(chart, index, *lane, *row, finish, revision))
            return *failure;
    return std::nullopt;
}

auto find_linked_sound_source(const chart_document_t& chart, object_id_t source_id,
                              local_position_t position, const semantic_event_refs_t& refs,
                              std::uint64_t revision)
    -> std::variant<linked_sound_source_t, document_command_result_t> {
    const auto channel = std::ranges::find_if(chart.sound_channels, [&](const auto& candidate) {
        return std::ranges::any_of(candidate.events,
                                   [&](const auto& event) { return event.id == source_id; });
    });
    if (channel == chart.sound_channels.end())
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id,
                        "BMSON sound assignment source event is unavailable");
    const auto source = std::ranges::find(channel->events, source_id,
                                           &sound_channel_event_t::id);
    const auto semantic_end = refs.note ? refs.note->end : refs.bgm->end;
    if (source == channel->events.end() || source->position != position ||
        source->end != semantic_end ||
        source->playable != (refs.note != nullptr) ||
        (refs.note && source->lane != refs.note->lane) || (refs.bgm && source->lane))
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id,
                        "BMSON semantic event and retained source disagree");
    if (std::ranges::any_of(channel->events, [&](const auto& other) {
            return other.id != source_id && other.position == position;
        }))
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::collision,
                        "Another event shares this BMSON source pulse");
    return linked_sound_source_t{&*channel, &*source};
}

auto ensure_bgm_source_lane(chart_document_t& chart, object_id_t channel, std::uint64_t order,
                            std::string_view channel_name, std::uint64_t revision)
    -> std::variant<object_id_t, document_command_result_t> {
    const auto owned = std::ranges::find(chart.bgm_source_lanes, std::optional{channel},
                                         &bgm_source_lane_t::stem_channel);
    if (owned != chart.bgm_source_lanes.end()) return owned->id;
    const auto found = std::ranges::find_if(chart.bgm_source_lanes,
        [&](const auto& lane) { return lane.source_order == order; });
    if (found != chart.bgm_source_lanes.end()) return found->id;
    auto largest = std::uint64_t{};
    const auto scan = [&](const auto& items) {
        for (const auto& item : items) largest = std::max(largest, item.id.value);
    };
    scan(chart.bgm_source_lanes); scan(chart.lane_layouts); scan(chart.bpms);
    scan(chart.pauses); scan(chart.warps); scan(chart.scrolls); scan(chart.speeds);
    scan(chart.kiai); scan(chart.timing_anchors); scan(chart.bookmarks); scan(chart.sections);
    scan(chart.bga_events); scan(chart.sound_channels);
    for (const auto& channel : chart.sound_channels) scan(channel.events);
    for (const auto& measure : chart.measures) {
        scan(measure.notes); scan(measure.bgm_events);
    }
    if (largest == std::numeric_limits<std::uint64_t>::max())
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id,
                        "BMSON BGM lane ID space is exhausted");
    const object_id_t id{largest + 1};
    const auto at = std::ranges::lower_bound(chart.bgm_source_lanes, order, {},
                                              &bgm_source_lane_t::source_order);
    chart.bgm_source_lanes.insert(at, {.id = id, .name = std::string{channel_name},
                                       .source_order = order});
    return id;
}

auto update_semantic_assignment(chart_document_t& chart, measure_chunk_t& measure,
                                const semantic_event_refs_t& refs,
                                object_id_t semantic_id, object_id_t source_id,
                                std::optional<lane_id_t> lane, std::uint64_t channel_order,
                                std::string_view channel_name, std::uint64_t revision)
    -> std::optional<document_command_result_t> {
    const auto position = refs.note ? refs.note->position : refs.bgm->position;
    if (lane) {
        if (refs.note) {
            const auto existing = std::ranges::find(measure.notes, semantic_id, &note_t::id);
            if (existing == measure.notes.end())
                return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id,
                                "Linked BMSON note disappeared during lane assignment");
            existing->lane = *lane;
            return std::nullopt;
        }
        measure.notes.push_back({.id = refs.bgm->id, .position = position, .lane = *lane,
            .kind = refs.bgm->end ? note_kind_t::hold : note_kind_t::tap,
            .end = refs.bgm->end, .sound = refs.bgm->sound,
            .sound_channel_event = source_id, .semantic_layers = refs.bgm->semantic_layers,
            .provenance = refs.bgm->provenance});
        std::erase_if(measure.bgm_events,
                      [semantic_id](const auto& item) { return item.id == semantic_id; });
        return std::nullopt;
    }
    auto lane_result = ensure_bgm_source_lane(chart, chart.sound_channels[channel_order].id,
                                               channel_order, channel_name, revision);
    if (const auto* failure = std::get_if<document_command_result_t>(&lane_result)) return *failure;
    const auto source_lane = std::get<object_id_t>(lane_result);
    measure.bgm_events.push_back({.id = refs.note->id, .position = position,
        .source_lane = source_lane, .sound = refs.note->sound,
        .sound_channel_event = source_id, .structural_slot = 0,
        .provenance = refs.note->provenance, .semantic_layers = refs.note->semantic_layers,
        .end = refs.note->end});
    std::erase_if(measure.notes,
                  [semantic_id](const auto& item) { return item.id == semantic_id; });
    return std::nullopt;
}

} // namespace

auto assign_bmson_sound_event(document_history_t& history, std::uint64_t expected_revision,
                              chart_id_t chart, object_id_t semantic_event,
                              std::optional<lane_id_t> playable_lane) -> document_command_result_t {
    const auto revision = history.document.revision;
    if (expected_revision != revision)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id,
                        "BMSON sound assignment has a stale document revision");
    const auto found_chart =
        std::ranges::find(history.document.charts, chart, &chart_document_t::id);
    if (!chart || found_chart == history.document.charts.end() ||
        found_chart->source_family != format_family_t::bmson || !semantic_event ||
        (playable_lane && !*playable_lane))
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id,
                        "BMSON sound assignment requires a linked event and valid target");
    const auto built = build_measure_index(*found_chart);
    if (!std::holds_alternative<measure_index_t>(built))
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_measure,
                        "BMSON sound assignment requires valid measure geometry");
    const auto& index = std::get<measure_index_t>(built);
    const auto refs = find_semantic_event(*found_chart, semantic_event);
    if (!valid_semantic_event(refs))
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id,
                        "BMSON sound assignment requires one supported linked event");
    const auto semantic_position = refs.note ? refs.note->position : refs.bgm->position;
    const auto semantic_end = refs.note ? refs.note->end : refs.bgm->end;
    if (semantic_end) {
        const auto begin = absolute_row(index, semantic_position);
        const auto finish = absolute_row(index, *semantic_end);
        if (!begin || !finish || *finish <= *begin)
            return document_command_transaction_t::rejection(revision, diagnostic_code_t::row_out_of_range,
                            "BMSON linked hold requires a positive in-chart span");
    }
    if ((refs.note && playable_lane && refs.note->lane == *playable_lane) ||
        (refs.bgm && !playable_lane))
        return {.revision = revision, .diagnostics = {}};
    if (auto failure = target_assignment_lane(*found_chart, index, refs, playable_lane, revision))
        return *failure;
    const auto position = refs.note ? refs.note->position : refs.bgm->position;
    const auto target_row = absolute_row(index, position);
    if (!target_row)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::row_out_of_range,
                        "BMSON sound assignment target is outside chart geometry");
    const auto source_id = refs.note ? *refs.note->sound_channel_event
                                     : *refs.bgm->sound_channel_event;
    const auto source_result = find_linked_sound_source(*found_chart, source_id, position,
                                                        refs, revision);
    if (const auto* failure = std::get_if<document_command_result_t>(&source_result))
        return *failure;
    const auto source = std::get<linked_sound_source_t>(source_result);
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    const auto chart_offset =
        static_cast<std::size_t>(found_chart - history.document.charts.begin());
    auto& changed_chart = proposed.charts[chart_offset];
    const auto channel_offset =
        static_cast<std::size_t>(source.channel - found_chart->sound_channels.data());
    const auto source_offset = static_cast<std::size_t>(source.event - source.channel->events.data());
    auto& changed_source = changed_chart.sound_channels[channel_offset].events[source_offset];
    changed_source.playable = playable_lane.has_value();
    changed_source.lane = playable_lane.value_or(lane_id_t{});
    const auto destination =
        std::ranges::find(changed_chart.measures, position.measure, &measure_chunk_t::id);
    if (destination == changed_chart.measures.end())
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_measure,
                        "BMSON sound assignment measure is unavailable");
    auto& changed_measure = *destination;
    if (auto failure = update_semantic_assignment(
            changed_chart, changed_measure, refs, semantic_event, source_id, playable_lane,
            channel_offset, source.channel->name, revision)) return *failure;
    if (auto failure = validate_channel_slices(
            proposed, changed_chart, changed_chart.sound_channels[channel_offset], revision))
        return *failure;
    return transaction.commit(playable_lane ? "Assign BMSON sound to playable lane"
                                                 : "Assign BMSON sound to BGM");
}

namespace {

auto find_chart_at_revision(document_history_t& history, std::uint64_t expected_revision,
                            chart_id_t chart_id)
    -> std::variant<std::pair<std::size_t, chart_document_t*>, document_command_result_t> {
    if (history.document.revision != expected_revision)
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON source insertion has a stale document revision");
    const auto chart = std::ranges::find(history.document.charts, chart_id,
                                         &chart_document_t::id);
    if (chart == history.document.charts.end() || !chart_id ||
        chart->source_family != format_family_t::bmson)
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON source insertion requires a known BMSON chart");
    return std::pair{static_cast<std::size_t>(chart - history.document.charts.begin()), &*chart};
}

using timing_arithmetic::subtract_exact;

auto geometry_fits_clip(const sound_channel_t& channel,
                        const bmson_source_geometry_t& geometry) -> bool {
    return geometry.sample_rate != 0 && geometry.frame_count != 0 &&
           channel.source_clip.begin_frame < geometry.frame_count &&
           (!channel.source_clip.end_frame ||
            (*channel.source_clip.end_frame > channel.source_clip.begin_frame &&
             *channel.source_clip.end_frame <= geometry.frame_count));
}

auto target_inside_active_slice(const sound_slice_result_t& slices, const timing_index_t& timing,
                                std::uint64_t target_row, std::uint32_t sample_rate,
                                std::optional<std::uint64_t> source_end) -> std::optional<bool> {
    const auto active = std::ranges::find_if(slices.slices.rbegin(), slices.slices.rend(),
        [target_row](const auto& slice) { return slice.absolute_row <= target_row; });
    if (active == slices.slices.rend()) return false;
    if (!source_end) return std::nullopt;
    if (active->source_begin_frame >= *source_end) return false;
    const auto start = timing_at(timing, active->absolute_row);
    const auto target = timing_at(timing, target_row);
    if (!start || !target) return std::nullopt;
    const auto elapsed = subtract_exact(*target.seconds, *start.seconds);
    if (!elapsed || elapsed->numerator < 0) return std::nullopt;
    const auto offset = sample_frame_at(*elapsed, sample_rate);
    if (!offset) return std::nullopt;
    if (offset.frame.value() > std::numeric_limits<std::uint64_t>::max() -
                                   active->source_begin_frame) return std::nullopt;
    const auto source_frame = active->source_begin_frame + offset.frame.value();
    const auto slice_end = active->source_end_frame.value_or(*source_end);
    return source_frame < std::min(slice_end, *source_end);
}

auto classify_stem_restart(const chart_document_t& chart, const sound_channel_t& channel,
                           const measure_index_t& measures, std::uint64_t row,
                           bool prefer_restart, std::uint32_t sample_rate,
                           std::optional<std::uint64_t> source_end,
                           std::uint64_t revision,
                           const sound_slice_result_t* validated_slices = nullptr)
    -> std::variant<bool, document_command_result_t> {
    if (prefer_restart || std::ranges::none_of(channel.events, [&](const auto& event) {
            const auto event_row = absolute_row(measures, event.position);
            return event_row && *event_row <= row;
        })) return true;
    if (sample_rate == 0)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_resolution,
                        "BMSON stem source sample rate is unavailable");
    std::optional<sound_slice_result_t> generated_slices;
    if (!validated_slices) {
        generated_slices = derive_sound_channel_slices(chart, channel, sample_rate, source_end);
        if (!*generated_slices)
            return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_resolution,
                            generated_slices->diagnostics.front().message);
        validated_slices = &*generated_slices;
    }
    const auto timing = build_timing_index(chart);
    if (!timing) return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_resolution,
                                 "BMSON stem point timing cannot be resolved");
    const auto inside = target_inside_active_slice(*validated_slices, *timing.index, row,
                                                    sample_rate, source_end);
    if (!inside) return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_resolution,
                                 "BMSON stem source length is unknown at the target row");
    return !*inside;
}

struct stem_source_t {
    const audio_asset_t* asset;
    const resource_entry_t* resource;
};

struct stem_target_t {
    std::size_t chart_offset;
    chart_document_t* chart;
    sound_channel_t* channel;
    bgm_source_lane_t* lane;
    measure_index_t measures;
    std::uint64_t row;
};

auto resolve_stem_target(document_history_t& history, std::size_t chart_offset,
                         chart_document_t& chart, object_id_t channel_id,
                         local_position_t target)
    -> std::variant<stem_target_t, document_command_result_t> {
    auto channel = std::ranges::find(chart.sound_channels, channel_id, &sound_channel_t::id);
    auto lane = std::ranges::find_if(chart.bgm_source_lanes,
        [channel_id](const auto& item) { return item.stem_channel == channel_id; });
    if (channel == chart.sound_channels.end() || lane == chart.bgm_source_lanes.end() ||
        std::ranges::count_if(chart.bgm_source_lanes, [channel_id](const auto& item) {
            return item.stem_channel == channel_id;
        }) != 1)
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON stem channel has no explicit linked source lane");
    const auto built = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(built))
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_measure,
                        "BMSON stem point requires valid chart geometry");
    auto measures = std::get<measure_index_t>(built);
    const auto row = absolute_row(measures, target);
    if (!row) return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::row_out_of_range,
                              "BMSON stem point is outside chart geometry");
    if (std::ranges::any_of(channel->events, [target](const auto& event) {
            return event.position == target;
        })) return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::collision,
                            "A point already occupies this stem channel row");
    return stem_target_t{chart_offset, &chart, &*channel, &*lane, std::move(measures), *row};
}

auto resolve_stem_source(const song_document_t& document, chart_id_t chart,
                         object_id_t channel, asset_id_t asset_id,
                         std::uint64_t revision)
    -> std::variant<stem_source_t, document_command_result_t> {
    const auto asset = std::ranges::find(document.audio_assets, asset_id, &audio_asset_t::id);
    const auto bindings = bmson_channel_resource_bindings(document, chart);
    const auto binding = std::ranges::find_if(bindings, [channel](const auto& candidate) {
        return candidate.channel == channel;
    });
    const auto count = std::ranges::count_if(bindings, [channel](const auto& candidate) {
        return candidate.channel == channel;
    });
    if (asset == document.audio_assets.end() || binding == bindings.end() ||
        binding->ambiguous || !binding->channel || count != 1)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::dangling_asset,
                        "BMSON stem channel has no unambiguous BGM resource binding");
    const auto resource = std::ranges::find(document.resources, binding->resource,
                                             &resource_entry_t::id);
    if (resource == document.resources.end() || resource->asset != asset_id)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::dangling_resource,
                        "BMSON stem channel resource binding does not match its audio asset");
    return stem_source_t{&*asset, &*resource};
}

auto resolve_stem_geometry(const chart_document_t& chart, const sound_channel_t& channel,
                           const audio_asset_t& asset,
                           std::optional<bmson_source_geometry_t> geometry,
                           std::uint64_t revision)
    -> std::variant<std::optional<sound_slice_result_t>, document_command_result_t> {
    if (geometry && (geometry->asset != asset.id || !geometry_fits_clip(channel, *geometry)))
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id,
                        "Decoded BMSON stem geometry does not match its source asset");
    if (!geometry && asset.stream.frame_count &&
        (channel.source_clip.begin_frame >= *asset.stream.frame_count ||
         (channel.source_clip.end_frame &&
          *channel.source_clip.end_frame > *asset.stream.frame_count)))
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_resolution,
                        "BMSON stem clip extends beyond its known audio source");
    if (!geometry) return std::optional<sound_slice_result_t>{};
    auto slices = derive_sound_channel_slices(chart, channel, geometry->sample_rate,
                                               geometry->frame_count);
    if (!slices)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_resolution,
                        slices.diagnostics.front().message);
    return std::optional{std::move(slices)};
}

struct empty_stem_fallbacks_t {
    std::unordered_map<std::uint64_t,
        std::unordered_map<std::string, std::vector<std::uint64_t>>> channels;
    std::unordered_map<std::uint64_t,
        std::unordered_map<std::string, std::size_t>> resource_counts;
};

template <typename Value>
auto lookup_nested(const std::unordered_map<std::uint64_t,
                    std::unordered_map<std::string, Value>>& values,
                   std::uint64_t asset, const std::string& name) -> const Value* {
    const auto outer = values.find(asset);
    if (outer == values.end()) return nullptr;
    const auto inner = outer->second.find(name);
    return inner == outer->second.end() ? nullptr : &inner->second;
}

auto has_resource_role(const resource_entry_t& resource, resource_role_t role) -> bool {
    return std::ranges::find(resource.roles, role) != resource.roles.end();
}

auto eligible_empty_stem_resource(const resource_entry_t& resource) -> bool {
    return !resource.missing && has_resource_role(resource, resource_role_t::bgm) &&
           (resource.sound_kind == sound_kind_t::stem ||
            resource.sound_kind == sound_kind_t::unspecified);
}

auto make_empty_stem_fallbacks(const song_document_t& document,
                               const chart_document_t& chart) -> empty_stem_fallbacks_t {
    empty_stem_fallbacks_t result;
    std::unordered_map<std::uint64_t, const sound_channel_t*> channels;
    for (const auto& channel : chart.sound_channels)
        channels.emplace(channel.id.value, &channel);
    for (const auto& lane : chart.bgm_source_lanes) {
        if (!lane.stem_channel) continue;
        const auto channel = channels.find(lane.stem_channel->value);
        if (channel == channels.end() || !channel->second->events.empty()) continue;
        result.channels[channel->second->source_clip.source.value]
            [channel->second->name].push_back(channel->first);
    }
    for (const auto& resource : document.resources) {
        if (!eligible_empty_stem_resource(resource)) continue;
        ++result.resource_counts[resource.asset.value][resource.name];
    }
    return result;
}

auto allocate_two_ids(chart_document_t& chart, std::uint64_t& largest,
                      std::uint64_t count) -> bool {
    const auto stats = chart_object_stats(chart);
    if (stats.count > default_maximum_chart_objects - count ||
        stats.largest > std::numeric_limits<std::uint64_t>::max() - count) return false;
    largest = stats.largest;
    return true;
}

auto add_bgm_projection(chart_document_t& chart, local_position_t target,
                        object_id_t source_event, resource_id_t resource,
                        object_id_t lane, object_id_t semantic_id,
                        std::uint64_t revision) -> std::optional<document_command_result_t> {
    const auto measure = std::ranges::find(chart.measures, target.measure, &measure_chunk_t::id);
    if (measure == chart.measures.end())
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_measure,
                        "BMSON source insertion measure is unavailable");
    measure->bgm_events.push_back({.id = semantic_id, .position = target,
        .source_lane = lane, .sound = resource, .sound_channel_event = source_event,
        .structural_slot = 0, .branch = std::nullopt, .provenance = {}});
    return std::nullopt;
}

using raw_channel_index_t =
    std::unordered_map<std::uint64_t, std::unordered_set<std::uint64_t>>;

auto index_source_events(const chart_document_t& chart) -> raw_channel_index_t {
    raw_channel_index_t result;
    for (const auto& channel : chart.sound_channels)
        for (const auto& event : channel.events)
            result[event.id.value].insert(channel.id.value);
    return result;
}

auto index_semantic_channels(const chart_document_t& chart,
                             const raw_channel_index_t& event_channels) -> raw_channel_index_t {
    raw_channel_index_t result;
    const auto record = [&](std::optional<resource_id_t> resource,
                            std::optional<object_id_t> event) {
        if (!resource || !event) return;
        const auto channels = event_channels.find(event->value);
        if (channels != event_channels.end())
            result[resource->value].insert(channels->second.begin(), channels->second.end());
    };
    for (const auto& measure : chart.measures) {
        for (const auto& event : measure.bgm_events)
            record(event.sound, event.sound_channel_event);
        for (const auto& event : measure.notes)
            record(event.sound, event.sound_channel_event);
    }
    return result;
}

auto resolve_resource_binding(const resource_entry_t& resource,
                              const raw_channel_index_t& semantic_channels,
                              const empty_stem_fallbacks_t& empty_stems)
    -> std::optional<bmson_channel_resource_binding_t> {
    const bool bgm = has_resource_role(resource, resource_role_t::bgm);
    const bool sample = has_resource_role(resource, resource_role_t::sample);
    if (resource.missing || (!bgm && !sample)) return std::nullopt;
    const auto linked = semantic_channels.find(resource.id.value);
    if (linked != semantic_channels.end()) {
        const bool ambiguous = linked->second.size() != 1;
        const auto channel = ambiguous ? std::nullopt
            : std::optional{object_id_t{*linked->second.begin()}};
        return bmson_channel_resource_binding_t{resource.id, channel, ambiguous};
    }
    if (!bgm || (resource.sound_kind != sound_kind_t::stem &&
                 resource.sound_kind != sound_kind_t::unspecified))
        return bmson_channel_resource_binding_t{resource.id, std::nullopt, false};
    const auto channels = lookup_nested(empty_stems.channels, resource.asset.value, resource.name);
    const auto resources = lookup_nested(empty_stems.resource_counts,
                                         resource.asset.value, resource.name);
    const auto channel_count = channels ? channels->size() : 0U;
    const auto resource_count = resources ? *resources : 0U;
    const bool ambiguous = channel_count > 1 || resource_count > 1;
    const auto channel = channel_count == 1 && resource_count == 1
        ? std::optional{object_id_t{channels->front()}} : std::nullopt;
    return bmson_channel_resource_binding_t{resource.id, channel, ambiguous};
}

} // namespace

auto bmson_channel_resource_bindings(const song_document_t& document, chart_id_t chart_id)
    -> std::vector<bmson_channel_resource_binding_t> {
    const auto chart = std::ranges::find(document.charts, chart_id, &chart_document_t::id);
    if (chart == document.charts.end() || chart->sound_channels.empty()) return {};

    const auto empty_stems = make_empty_stem_fallbacks(document, *chart);
    const auto event_channels = index_source_events(*chart);
    const auto semantic_channels = index_semantic_channels(*chart, event_channels);
    std::vector<bmson_channel_resource_binding_t> result;
    for (const auto& resource : document.resources) {
        if (auto binding = resolve_resource_binding(resource, semantic_channels, empty_stems))
            result.push_back(*binding);
    }
    return result;
}

auto insert_bmson_stem_point(document_history_t& history, std::uint64_t expected_revision,
                             chart_id_t chart_id, object_id_t channel_id,
                             local_position_t target, bool prefer_restart,
                             std::optional<bmson_source_geometry_t> geometry)
    -> document_command_result_t {
    const auto resolved = find_chart_at_revision(history, expected_revision, chart_id);
    if (const auto* failure = std::get_if<document_command_result_t>(&resolved)) return *failure;
    const auto [chart_offset, chart] =
        std::get<std::pair<std::size_t, chart_document_t*>>(resolved);
    const auto target_context = resolve_stem_target(history, chart_offset, *chart,
                                                     channel_id, target);
    if (const auto* failure = std::get_if<document_command_result_t>(&target_context))
        return *failure;
    const auto context = std::get<stem_target_t>(target_context);
    auto* channel = context.channel;
    auto* lane = context.lane;
    auto* original_chart = context.chart;
    const auto source = resolve_stem_source(history.document, chart_id, channel_id,
        channel->source_clip.source, history.document.revision);
    if (const auto* failure = std::get_if<document_command_result_t>(&source)) return *failure;
    const auto stem_source = std::get<stem_source_t>(source);
    const auto validated = resolve_stem_geometry(*original_chart, *channel,
        *stem_source.asset, geometry, history.document.revision);
    if (const auto* failure = std::get_if<document_command_result_t>(&validated)) return *failure;
    const auto decoded_slices = std::get<std::optional<sound_slice_result_t>>(validated);
    const auto rate = geometry ? geometry->sample_rate : stem_source.asset->stream.sample_rate;
    const auto source_end = geometry ? std::optional{geometry->frame_count}
                                     : stem_source.asset->stream.frame_count;
    const auto known_end = source_end ? source_end : channel->source_clip.end_frame;
    const auto classified = classify_stem_restart(*original_chart, *channel,
        context.measures, context.row, prefer_restart, rate, known_end,
        history.document.revision, decoded_slices ? &*decoded_slices : nullptr);
    if (const auto* failure = std::get_if<document_command_result_t>(&classified)) return *failure;
    const bool restart = std::get<bool>(classified);
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    auto& changed = proposed.charts[context.chart_offset];
    auto& changed_channel = *std::ranges::find(changed.sound_channels, channel_id,
                                                 &sound_channel_t::id);
    std::uint64_t largest{};
    if (!allocate_two_ids(changed, largest, 2))
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON chart object ID space is exhausted");
    const object_id_t source_event{++largest};
    const object_id_t semantic{++largest};
    changed_channel.events.push_back({.id = source_event, .position = target,
        .end = std::nullopt, .lane = {}, .continuation = !restart, .playable = false,
        .provenance = {}});
    if (auto failure = add_bgm_projection(changed, target, source_event,
                                              stem_source.resource->id,
                                              lane->id, semantic, history.document.revision))
        return *failure;
    if (geometry) {
        const auto slices = derive_sound_channel_slices(changed, changed_channel,
            geometry->sample_rate, geometry->frame_count);
        if (!slices)
            return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_resolution,
                            slices.diagnostics.front().message);
    } else if (auto failure = validate_channel_slices(proposed, changed,
                                                       changed_channel, history.document.revision)) {
        return *failure;
    }
    return transaction.commit("Insert BMSON stem point");
}

namespace {
auto reuse_playable_stem_pulse(document_history_t& history, chart_id_t chart_id,
    object_id_t channel_id, local_position_t target, lane_id_t lane)
    -> std::optional<document_command_result_t> {
    const auto chart = std::ranges::find(history.document.charts, chart_id, &chart_document_t::id);
    if (chart == history.document.charts.end()) return std::nullopt;
    const auto channel = std::ranges::find(chart->sound_channels, channel_id, &sound_channel_t::id);
    if (channel == chart->sound_channels.end()) return std::nullopt;
    const auto source = std::ranges::find(channel->events, target, &sound_channel_event_t::position);
    if (source == channel->events.end()) return std::nullopt;
    const auto source_id = source->id;
    for (const auto& measure : chart->measures) {
        const auto bgm = std::ranges::find(measure.bgm_events, std::optional{source_id},
                                         &bgm_event_t::sound_channel_event);
        if (bgm != measure.bgm_events.end())
            return assign_bmson_sound_event(history, history.document.revision, chart_id, bgm->id, lane);
    }
    for (const auto& measure : chart->measures) {
        const auto note = std::ranges::find(measure.notes, std::optional{source_id},
                                          &note_t::sound_channel_event);
        if (note != measure.notes.end())
            return assign_bmson_sound_event(history, history.document.revision, chart_id, note->id, lane);
    }
    return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                    "BMSON stem pulse has no semantic projection");
}
}

auto insert_bmson_playable_stem_point(document_history_t& history,
    std::uint64_t expected_revision, chart_id_t chart_id, object_id_t channel_id,
    local_position_t target, lane_id_t lane, std::optional<bmson_source_geometry_t> geometry)
    -> document_command_result_t {
    if (history.document.revision != expected_revision)
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON stem entry expired after the document changed");
    if (const auto reused = reuse_playable_stem_pulse(history, chart_id, channel_id, target, lane))
        return *reused;
    auto scratch = make_document_history(history.document);
    const auto inserted = insert_bmson_stem_point(scratch, scratch.document.revision,
        chart_id, channel_id, target, false, geometry);
    if (!inserted) return inserted;
    const auto& chart = *std::ranges::find(scratch.document.charts, chart_id, &chart_document_t::id);
    const auto& channel = *std::ranges::find(chart.sound_channels, channel_id, &sound_channel_t::id);
    const auto source = std::ranges::find(channel.events, target, &sound_channel_event_t::position);
    for (const auto& measure : chart.measures)
        for (const auto& event : measure.bgm_events) {
            if (event.sound_channel_event != source->id) continue;
            const auto assigned = assign_bmson_sound_event(scratch, scratch.document.revision,
                chart_id, event.id, lane);
            if (!assigned) return assigned;
            auto transaction = document_command_transaction_t::from(history);
            transaction.adopt_document(std::move(scratch.document));
            return transaction.commit("Place linked BMSON stem note");
        }
    return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                    "BMSON stem entry could not find its linked projection");
}

namespace {
auto append_packed_sound_channel(song_document_t& document, chart_document_t& chart,
                                  resource_id_t resource, std::uint64_t& largest)
    -> source_provenance_t;
} // namespace

auto insert_bmson_packed_one_shot(document_history_t& history,
                                  std::uint64_t expected_revision, chart_id_t chart_id,
                                  object_id_t source_lane_id, resource_id_t resource_id,
                                  local_position_t target) -> document_command_result_t {
    const auto resolved = find_chart_at_revision(history, expected_revision, chart_id);
    if (const auto* failure = std::get_if<document_command_result_t>(&resolved)) return *failure;
    const auto [chart_offset, original_chart] =
        std::get<std::pair<std::size_t, chart_document_t*>>(resolved);
    const auto lane = std::ranges::find(original_chart->bgm_source_lanes, source_lane_id,
                                         &bgm_source_lane_t::id);
    const auto resource = std::ranges::find(history.document.resources, resource_id,
                                             &resource_entry_t::id);
    const auto bindings = bmson_channel_resource_bindings(history.document, chart_id);
    const auto resource_binding = std::ranges::find(bindings, resource_id,
        &bmson_channel_resource_binding_t::resource);
    if (lane == original_chart->bgm_source_lanes.end() || lane->stem_channel ||
        resource == history.document.resources.end() || resource->missing ||
        resource->sound_kind == sound_kind_t::stem ||
        (std::ranges::find(resource->roles, resource_role_t::sample) == resource->roles.end() &&
         std::ranges::find(resource->roles, resource_role_t::bgm) == resource->roles.end()))
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::dangling_resource,
                        "Packed BMSON points require an unlinked lane and sample/BGM resource");
    const auto asset = std::ranges::find(history.document.audio_assets, resource->asset,
                                          &audio_asset_t::id);
    if (asset == history.document.audio_assets.end())
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::dangling_asset,
                        "Packed BMSON sound resource has no audio asset");
    const auto built = build_measure_index(*original_chart);
    if (!std::holds_alternative<measure_index_t>(built) ||
        !absolute_row(std::get<measure_index_t>(built), target))
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::row_out_of_range,
                        "Packed BMSON point is outside chart geometry");
    if (resource_binding != bindings.end() && resource_binding->ambiguous)
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "Packed BMSON resource has ambiguous sound-channel bindings");
    const auto channel = resource_binding != bindings.end() && resource_binding->channel
        ? std::ranges::find(original_chart->sound_channels, *resource_binding->channel,
                            &sound_channel_t::id)
        : original_chart->sound_channels.end();
    if (channel != original_chart->sound_channels.end()) {
        const auto stem_lane = std::ranges::find_if(original_chart->bgm_source_lanes,
            [&](const auto& source_lane) { return source_lane.stem_channel == channel->id; });
        if (stem_lane != original_chart->bgm_source_lanes.end())
            return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::collision,
                            "Resource belongs to stem lane '" + stem_lane->name + "'");
    }
    if (channel != original_chart->sound_channels.end() &&
        std::ranges::any_of(channel->events, [target](const auto& event) {
            return event.position == target;
        })) return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::collision,
                            "Packed BMSON source channel already has a point at this row");
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    auto& changed = proposed.charts[chart_offset];
    std::uint64_t largest{};
    const auto ids_needed = channel == original_chart->sound_channels.end() ? 4U : 2U;
    if (!allocate_two_ids(changed, largest, ids_needed))
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON chart object ID space is exhausted");
    sound_channel_t* destination{};
    source_provenance_t provenance;
    if (channel == original_chart->sound_channels.end()) {
        provenance = append_packed_sound_channel(proposed, changed, resource_id, largest);
        destination = &changed.sound_channels.back();
    } else {
        const auto offset = static_cast<std::size_t>(channel - original_chart->sound_channels.begin());
        destination = &changed.sound_channels[offset];
    }
    const object_id_t source_event{++largest};
    const object_id_t semantic{++largest};
    destination->events.push_back({.id = source_event, .position = target,
        .end = std::nullopt, .lane = {}, .continuation = false, .playable = false,
        .provenance = provenance});
    if (auto failure = add_bgm_projection(changed, target, source_event, resource_id,
                                          source_lane_id, semantic,
                                          history.document.revision)) return *failure;
    if (auto failure = validate_channel_slices(proposed, changed, *destination,
                                                history.document.revision)) return *failure;
    return transaction.commit("Insert packed BMSON one-shot");
}

// T-484: atomic stroke commands for packed (unlinked) BMSON keysound columns.
namespace {

struct packed_stroke_cell_t {
    local_position_t position;
    std::uint64_t row{};
};

struct packed_stroke_context_t {
    std::size_t chart_offset{};
    const chart_document_t* chart{};
    measure_index_t index;
    std::vector<packed_stroke_cell_t> cells;
};

auto packed_stroke_unchanged(const document_history_t& history) -> document_command_result_t {
    return {.revision = history.document.revision, .diagnostics = {}};
}

auto resolve_packed_stroke_context(document_history_t& history, std::uint64_t revision,
                                   chart_id_t chart_id, object_id_t lane_id,
                                   std::span<const local_position_t> targets)
    -> std::variant<packed_stroke_context_t, document_command_result_t> {
    const auto resolved = find_chart_at_revision(history, revision, chart_id);
    if (const auto* failure = std::get_if<document_command_result_t>(&resolved)) return *failure;
    const auto [chart_offset, chart] =
        std::get<std::pair<std::size_t, chart_document_t*>>(resolved);
    const auto lane = std::ranges::find(chart->bgm_source_lanes, lane_id, &bgm_source_lane_t::id);
    if (lane == chart->bgm_source_lanes.end())
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON stroke refers to an unknown source lane");
    if (lane->stem_channel)
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::unsupported_feature,
                        "BMSON stem lanes are not drawable; place stem points on a stem lane instead");
    auto built = build_measure_index(*chart);
    auto* index = std::get_if<measure_index_t>(&built);
    if (!index)
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_measure,
                        "BMSON stroke chart index is invalid");
    packed_stroke_context_t context{chart_offset, chart, std::move(*index), {}};
    std::unordered_set<std::uint64_t> seen;
    for (const auto& position : targets) {
        const auto row = absolute_row(context.index, position);
        if (!row)
            return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::row_out_of_range,
                            "Packed BMSON stroke row is outside chart geometry");
        if (seen.insert(*row).second) context.cells.push_back({position, *row});
    }
    return context;
}

auto packed_stroke_resource_failure(const document_history_t& history, const chart_document_t& chart,
                                    const resource_entry_t* resource)
    -> std::optional<document_command_result_t> {
    if (!resource || resource->missing || resource->sound_kind == sound_kind_t::stem ||
        (std::ranges::find(resource->roles, resource_role_t::sample) == resource->roles.end() &&
         std::ranges::find(resource->roles, resource_role_t::bgm) == resource->roles.end()))
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::dangling_resource,
                        "Packed BMSON points require an unlinked lane and sample/BGM resource");
    if (std::ranges::find(history.document.audio_assets, resource->asset, &audio_asset_t::id) ==
        history.document.audio_assets.end())
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::dangling_asset,
                        "Packed BMSON sound resource has no audio asset");
    const auto bindings = bmson_channel_resource_bindings(history.document, chart.id);
    const auto binding = std::ranges::find(bindings, resource->id,
                                           &bmson_channel_resource_binding_t::resource);
    if (binding == bindings.end() || !binding->channel) {
        return std::nullopt;
    }
    if (binding->ambiguous)
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "Packed BMSON resource has ambiguous sound-channel bindings");
    const auto stem_lane = std::ranges::find_if(chart.bgm_source_lanes, [&](const auto& lane) {
        return lane.stem_channel == binding->channel;
    });
    if (stem_lane == chart.bgm_source_lanes.end()) return std::nullopt;
    return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::collision,
                    "Resource belongs to stem lane '" + stem_lane->name + "'");
}

auto packed_stroke_channel(const document_history_t& history, const chart_document_t& chart,
                           resource_id_t resource) -> std::optional<object_id_t> {
    const auto bindings = bmson_channel_resource_bindings(history.document, chart.id);
    const auto binding = std::ranges::find(bindings, resource,
                                           &bmson_channel_resource_binding_t::resource);
    return binding == bindings.end() ? std::nullopt : binding->channel;
}

// Rows at which the lane already shows `resource` through a packed channel event.
auto packed_lane_rows(const chart_document_t& chart, const measure_index_t& index,
                      object_id_t lane, resource_id_t resource)
    -> std::unordered_map<std::uint64_t, object_id_t> {
    std::unordered_map<std::uint64_t, object_id_t> rows;
    for (const auto& measure : chart.measures)
        for (const auto& event : measure.bgm_events) {
            if (event.source_lane != lane || !event.sound_channel_event ||
                event.sound != std::optional{resource}) continue;
            if (const auto row = absolute_row(index, event.position))
                rows.emplace(*row, *event.sound_channel_event);
        }
    return rows;
}

auto packed_stroke_additions(const packed_stroke_context_t& context, const sound_channel_t* channel,
                             const std::unordered_map<std::uint64_t, object_id_t>& shown)
    -> std::variant<std::vector<packed_stroke_cell_t>, std::string> {
    std::unordered_set<std::uint64_t> channel_rows;
    if (channel)
        for (const auto& event : channel->events)
            if (const auto row = absolute_row(context.index, event.position))
                channel_rows.insert(*row);
    std::vector<packed_stroke_cell_t> additions;
    for (const auto& cell : context.cells) {
        if (shown.contains(cell.row)) continue;
        if (channel_rows.contains(cell.row))
            return std::string{"Packed BMSON source channel already has a point at this row"};
        additions.push_back(cell);
    }
    return additions;
}

// The BMSON writer pairs a resource with its channel through a "sound_channel" resource
// identity that matches the "$.sound_channels[N]" provenance of the channel's events, as
// the importer records them. A channel created by a stroke gets the same pairing so the
// chart exports; an existing identity is reused.
auto packed_channel_source_index(resource_entry_t& resource, const song_document_t& document)
    -> std::size_t {
    const auto parse = [](std::string_view text) -> std::optional<std::size_t> {
        std::size_t value{};
        const auto* end = text.data() + text.size();
        const auto parsed = std::from_chars(text.data(), end, value);
        return parsed.ec == std::errc{} && parsed.ptr == end ? std::optional{value} : std::nullopt;
    };
    const auto is_channel_identity = [](const auto& identity) {
        return identity.family == format_family_t::bmson && identity.namespace_name == "sound_channel";
    };
    if (const auto own = std::ranges::find_if(resource.target_identities, is_channel_identity);
        own != resource.target_identities.end())
        if (const auto value = parse(own->identifier)) return *value;
    std::size_t next{};
    for (const auto& other : document.resources)
        for (const auto& identity : other.target_identities)
            if (is_channel_identity(identity))
                if (const auto value = parse(identity.identifier)) next = std::max(next, *value + 1);
    for (const auto& chart : document.charts) next = std::max(next, chart.sound_channels.size());
    resource.target_identities.push_back({format_family_t::bmson, std::to_string(next),
                                          "sound_channel"});
    return next;
}

auto append_packed_sound_channel(song_document_t& document, chart_document_t& chart,
                                  resource_id_t resource, std::uint64_t& largest)
    -> source_provenance_t {
    auto& owner = *std::ranges::find(document.resources, resource, &resource_entry_t::id);
    const auto source_index = packed_channel_source_index(owner, document);
    const clip_id_t clip_id{++largest};
    chart.sound_channels.push_back({.id = object_id_t{++largest}, .name = owner.name,
        .source_clip = {.id = clip_id, .source = owner.asset, .begin_frame = 0,
                        .end_frame = std::nullopt}, .events = {}});
    return {.family = format_family_t::bmson,
            .source_field = "$.sound_channels[" + std::to_string(source_index) + "]"};
}

auto commit_packed_stroke_draw(document_history_t& history, const packed_stroke_context_t& context,
                               object_id_t lane_id, const resource_entry_t& resource)
    -> document_command_result_t {
    const auto channel_id = packed_stroke_channel(history, *context.chart, resource.id);
    const auto* channel = channel_id ? &*std::ranges::find(context.chart->sound_channels,
        *channel_id, &sound_channel_t::id) : nullptr;
    const auto shown = packed_lane_rows(*context.chart, context.index, lane_id, resource.id);
    auto additions = packed_stroke_additions(context, channel, shown);
    if (const auto* message = std::get_if<std::string>(&additions))
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::collision, *message);
    const auto& cells = std::get<std::vector<packed_stroke_cell_t>>(additions);
    if (cells.empty()) return packed_stroke_unchanged(history);
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    auto& changed = proposed.charts[context.chart_offset];
    std::uint64_t largest{};
    if (!allocate_two_ids(changed, largest, cells.size() * 2 + (channel ? 0 : 2)))
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON chart object ID space is exhausted");
    source_provenance_t provenance;
    if (!channel) {
        provenance = append_packed_sound_channel(proposed, changed, resource.id, largest);
    }
    auto& destination = channel ? *std::ranges::find(changed.sound_channels, *channel_id,
                                                      &sound_channel_t::id)
                                : changed.sound_channels.back();
    for (const auto& cell : cells) {
        const object_id_t source_event{++largest};
        const object_id_t semantic{++largest};
        destination.events.push_back({.id = source_event, .position = cell.position,
            .end = std::nullopt, .lane = {}, .continuation = false, .playable = false,
            .provenance = provenance});
        if (auto failure = add_bgm_projection(changed, cell.position, source_event, resource.id,
                                              lane_id, semantic, history.document.revision))
            return *failure;
    }
    if (auto failure = validate_channel_slices(proposed, changed, destination,
                                               history.document.revision)) return *failure;
    return transaction.commit("Draw packed BMSON events");
}

auto commit_packed_stroke_erase(document_history_t& history, const packed_stroke_context_t& context,
                                object_id_t lane_id) -> document_command_result_t {
    std::unordered_set<std::uint64_t> rows;
    for (const auto& cell : context.cells) rows.insert(cell.row);
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    auto& changed = proposed.charts[context.chart_offset];
    std::unordered_set<std::uint64_t> removed_events;
    for (auto& measure : changed.measures)
        std::erase_if(measure.bgm_events, [&](const auto& event) {
            const auto row = event.source_lane == lane_id && event.sound_channel_event
                ? absolute_row(context.index, event.position) : std::nullopt;
            if (!row || !rows.contains(*row)) return false;
            removed_events.insert(event.sound_channel_event->value);
            return true;
        });
    if (removed_events.empty()) return packed_stroke_unchanged(history);
    std::vector<const sound_channel_t*> touched;
    for (auto& channel : changed.sound_channels)
        if (std::erase_if(channel.events, [&](const auto& event) {
                return removed_events.contains(event.id.value);
            })) touched.push_back(&channel);
    for (const auto* channel : touched)
        if (auto failure = validate_channel_slices(proposed, changed, *channel,
                                                   history.document.revision)) return *failure;
    return transaction.commit("Erase packed BMSON events");
}

} // namespace

auto apply_bmson_packed_stroke(document_history_t& history, std::uint64_t expected_revision,
                               chart_id_t chart_id, object_id_t source_lane,
                               std::optional<resource_id_t> resource,
                               std::span<const local_position_t> targets, bool erase)
    -> document_command_result_t {
    if (targets.size() > 50'000)
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_resolution,
                        "BMSON stroke exceeds the per-gesture target cap");
    const auto resolved = resolve_packed_stroke_context(history, expected_revision, chart_id,
                                                        source_lane, targets);
    if (const auto* failure = std::get_if<document_command_result_t>(&resolved)) return *failure;
    const auto& context = std::get<packed_stroke_context_t>(resolved);
    if (context.cells.empty()) return packed_stroke_unchanged(history);
    if (erase) return commit_packed_stroke_erase(history, context, source_lane);
    const auto entry = resource ? std::ranges::find(history.document.resources, *resource,
                                                    &resource_entry_t::id)
                                : history.document.resources.end();
    const auto* found = entry == history.document.resources.end() ? nullptr : &*entry;
    if (auto failure = packed_stroke_resource_failure(history, *context.chart, found))
        return *failure;
    return commit_packed_stroke_draw(history, context, source_lane, *found);
}

} // namespace wormedit
