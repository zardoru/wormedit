#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/measure.h>
#include <wormedit/core/sound_slice.h>

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <unordered_map>

namespace wormedit {
namespace {

auto rejected(std::uint64_t revision, diagnostic_code_t code, std::string message)
    -> document_command_result_t {
    return {.applied = false,
            .revision = revision,
            .diagnostics = {{.severity = diagnostic_severity_t::error,
                             .code = code,
                             .message = std::move(message),
                             .object = std::nullopt}}};
}

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
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON restart edit has a stale document revision");
    }
    const auto found_chart = std::ranges::find(history.document.charts, chart,
                                                &chart_document_t::id);
    if (!chart || found_chart == history.document.charts.end() ||
        found_chart->source_family != format_family_t::bmson || !sound_channel_event) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON restart edit requires a known BMSON chart and source event");
    }
    const auto channel_index = std::ranges::find_if(found_chart->sound_channels,
        [&](const auto& channel) {
            return std::ranges::any_of(channel.events, [&](const auto& event) {
                return event.id == sound_channel_event;
            });
        });
    if (channel_index == found_chart->sound_channels.end()) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON source event is not in the selected chart");
    }
    const auto target = std::ranges::find(channel_index->events, sound_channel_event,
                                           &sound_channel_event_t::id);
    if (target == channel_index->events.end() || target->playable) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
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
    if (!needs_change) return {.applied = false, .revision = history.document.revision};
    auto proposed = history.document;
    auto& events = proposed.charts[target.chart_offset].sound_channels[target.channel_offset].events;
    for (auto& event : events) {
        if (event.position == target.position) event.continuation = !restart;
    }
    return commit_document_command(history, std::move(proposed), description);
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
    const auto step = snap_denominator == 0
        ? std::uint64_t{1}
        : chart.divisions_per_standard_measure / snap_denominator;
    if (step == 0 || *first % step != 0 || *last % step != 0 ||
        (*first > *last ? *first - *last : *last - *first) / step > maximum_positions) {
        reject_path(diagnostic_code_t::invalid_resolution,
                    "BMSON razor path exceeds its grid or marker capacity");
        return result;
    }
    const auto count = (*first > *last ? *first - *last : *last - *first) / step;
    result.positions.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t offset = 1; offset <= count; ++offset) {
        const auto row = *first < *last ? *first + offset * step : *first - offset * step;
        const auto after = std::ranges::upper_bound(index.entries, row, {},
                                                     &measure_index_entry_t::absolute_begin_row);
        if (after == index.entries.begin()) {
            reject_path(diagnostic_code_t::invalid_measure,
                        "BMSON razor path cannot resolve a measure boundary");
            result.positions.clear();
            return result;
        }
        const auto entry = std::prev(after);
        result.positions.push_back({entry->id, row - entry->absolute_begin_row});
    }
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

auto move_bmson_bgm_marker(document_history_t& history, std::uint64_t expected_revision,
                           chart_id_t chart, object_id_t bgm_event, local_position_t target)
    -> document_command_result_t {
    if (expected_revision != history.document.revision) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON marker move has a stale document revision");
    }
    const auto found_chart = std::ranges::find(history.document.charts, chart,
                                                &chart_document_t::id);
    if (!chart || found_chart == history.document.charts.end() ||
        found_chart->source_family != format_family_t::bmson || !bgm_event) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON marker move requires a known chart and BGM event");
    }
    const auto measures = build_measure_index(*found_chart);
    if (!std::holds_alternative<measure_index_t>(measures) ||
        !absolute_row(std::get<measure_index_t>(measures), target)) {
        return rejected(history.document.revision, diagnostic_code_t::row_out_of_range,
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
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BGM marker has no retained BMSON sound-channel event");
    }
    if (semantic->position == target) {
        return {.applied = false, .revision = history.document.revision, .diagnostics = {}};
    }
    const auto channel = std::ranges::find_if(found_chart->sound_channels,
        [&](const auto& candidate) {
            return std::ranges::any_of(candidate.events, [&](const auto& event) {
                return event.id == *semantic->sound_channel_event;
            });
        });
    if (channel == found_chart->sound_channels.end()) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BGM marker source event is unavailable");
    }
    const auto source = std::ranges::find(channel->events, *semantic->sound_channel_event,
                                           &sound_channel_event_t::id);
    if (source == channel->events.end() || source->playable ||
        source->position != semantic->position) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BGM marker and retained source event disagree");
    }
    if (std::ranges::any_of(channel->events, [&](const auto& event) {
            return event.id != source->id && event.position == target;
        })) {
        return rejected(history.document.revision, diagnostic_code_t::collision,
                        "Another event in this BMSON sound channel occupies the target row");
    }
    auto proposed = history.document;
    const auto chart_offset = static_cast<std::size_t>(found_chart - history.document.charts.begin());
    auto& changed_chart = proposed.charts[chart_offset];
    const auto channel_offset = static_cast<std::size_t>(channel - found_chart->sound_channels.begin());
    const auto source_offset = static_cast<std::size_t>(source - channel->events.begin());
    changed_chart.sound_channels[channel_offset].events[source_offset].position = target;
    for (auto& measure : changed_chart.measures) {
        std::erase_if(measure.bgm_events, [bgm_event](const auto& event) {
            return event.id == bgm_event;
        });
    }
    const auto destination = std::ranges::find(changed_chart.measures, target.measure,
                                                &measure_chunk_t::id);
    if (destination == changed_chart.measures.end()) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_measure,
                        "BMSON marker destination measure is unavailable");
    }
    auto moved = *semantic;
    moved.position = target;
    destination->bgm_events.push_back(std::move(moved));
    const auto asset = std::ranges::find(proposed.audio_assets,
        changed_chart.sound_channels[channel_offset].source_clip.source,
        &audio_asset_t::id);
    if (asset != proposed.audio_assets.end() && asset->stream.sample_rate != 0) {
        const auto slices = derive_sound_channel_slices(
            changed_chart, changed_chart.sound_channels[channel_offset],
            asset->stream.sample_rate, asset->stream.frame_count);
        if (!slices) {
            return rejected(history.document.revision, diagnostic_code_t::invalid_resolution,
                            slices.diagnostics.front().message);
        }
    }
    return commit_document_command(history, std::move(proposed), "Move BMSON BGM marker");
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
        return rejected(revision, diagnostic_code_t::invalid_id,
                       "Reference BGM marker has no retained BMSON sound event");
    const auto channel = std::ranges::find_if(chart.sound_channels, [&](const auto& candidate) {
        return std::ranges::any_of(candidate.events, [&](const auto& event) {
            return event.id == *marker->sound_channel_event;
        });
    });
    if (channel == chart.sound_channels.end())
        return rejected(revision, diagnostic_code_t::invalid_id,
                       "Reference BGM source event is unavailable");
    const auto source = std::ranges::find(channel->events, *marker->sound_channel_event,
                                          &sound_channel_event_t::id);
    if (source == channel->events.end() || source->playable || source->position != marker->position)
        return rejected(revision, diagnostic_code_t::invalid_id,
                       "Reference BGM marker and retained source event disagree");
    return marker_source_t{marker, &*channel, &*source};
}

auto validate_marker_batch_geometry(const measure_index_t& index,
                                    std::span<const bmson_sound_marker_insertion_t> markers,
                                    std::uint64_t revision)
    -> std::optional<document_command_result_t> {
    for (std::size_t marker = 0; marker < markers.size(); ++marker) {
        if (!absolute_row(index, markers[marker].position))
            return rejected(revision, diagnostic_code_t::row_out_of_range,
                            "BMSON marker target is outside exact chart geometry");
        if (std::ranges::any_of(markers.first(marker), [&](const auto& prior) {
                return prior.position == markers[marker].position;
            }))
            return rejected(revision, diagnostic_code_t::collision,
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
                return rejected(revision, diagnostic_code_t::row_out_of_range,
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
            return rejected(revision, diagnostic_code_t::collision,
                            "Another event in this BMSON sound channel occupies the target row");
        if (!marker.playable_lane) continue;
        const auto row = absolute_row(index, marker.position);
        if (!*marker.playable_lane || !row)
            return rejected(revision, diagnostic_code_t::unknown_lane,
                            "BMSON marker has an invalid playable lane");
        if (!editable_lane_at(chart, index, *marker.playable_lane, *row))
            return rejected(revision, diagnostic_code_t::unknown_lane,
                            "BMSON marker playable lane is unavailable at its row");
        const auto& intervals = occupied.at(marker.playable_lane->value);
        const auto after = std::upper_bound(intervals.begin(), intervals.end(), *row,
            [](std::uint64_t value, const auto& interval) { return value < interval.first; });
        if (after != intervals.begin() && std::prev(after)->second >= *row)
            return rejected(revision, diagnostic_code_t::collision,
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
    for (const auto& channel : chart.sound_channels) scan(channel.events);
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
        return rejected(revision, diagnostic_code_t::invalid_measure,
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
    return rejected(revision, diagnostic_code_t::invalid_resolution,
                    slices.diagnostics.front().message);
}

} // namespace

auto insert_bmson_sound_markers(document_history_t& history, std::uint64_t expected_revision,
                                chart_id_t chart, object_id_t reference_bgm_event,
                                std::span<const bmson_sound_marker_insertion_t> markers)
    -> document_command_result_t {
    const auto revision = history.document.revision;
    if (expected_revision != revision)
        return rejected(revision, diagnostic_code_t::invalid_id,
                        "BMSON marker insertion has a stale document revision");
    if (markers.empty() || markers.size() > 256)
        return rejected(revision, diagnostic_code_t::invalid_id,
                        "BMSON marker insertion requires 1 to 256 markers");
    const auto found_chart = std::ranges::find(history.document.charts, chart,
                                                &chart_document_t::id);
    if (!chart || found_chart == history.document.charts.end() ||
        found_chart->source_family != format_family_t::bmson || !reference_bgm_event)
        return rejected(revision, diagnostic_code_t::invalid_id,
                        "BMSON marker insertion requires a known chart and BGM event");
    const auto built = build_measure_index(*found_chart);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built))
        return rejected(revision, diagnostic_code_t::invalid_measure,
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
    if (stats.count > 1'000'000 - needed ||
        stats.largest > std::numeric_limits<std::uint64_t>::max() - needed)
        return rejected(revision, diagnostic_code_t::invalid_id,
                        "BMSON chart object ID space is exhausted");

    auto proposed = history.document;
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
    return commit_document_command(history, std::move(proposed),
        markers.size() == 1 ? "Insert BMSON sound marker" : "Insert BMSON sound markers");
}

auto delete_bmson_bgm_marker(document_history_t& history,
                             std::uint64_t expected_revision, chart_id_t chart,
                             object_id_t bgm_event) -> document_command_result_t {
    if (expected_revision != history.document.revision) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON marker deletion has a stale document revision");
    }
    const auto found_chart = std::ranges::find(history.document.charts, chart,
                                                &chart_document_t::id);
    if (!chart || found_chart == history.document.charts.end() ||
        found_chart->source_family != format_family_t::bmson || !bgm_event) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
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
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BGM marker has no retained BMSON sound-channel event");
    }
    const auto channel = std::ranges::find_if(found_chart->sound_channels,
        [&](const auto& candidate) {
            return std::ranges::any_of(candidate.events, [&](const auto& event) {
                return event.id == *semantic->sound_channel_event;
            });
        });
    if (channel == found_chart->sound_channels.end()) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BGM marker source event is unavailable");
    }
    const auto source = std::ranges::find(channel->events, *semantic->sound_channel_event,
                                           &sound_channel_event_t::id);
    if (source == channel->events.end() || source->playable ||
        source->position != semantic->position) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BGM marker and retained source event disagree");
    }
    auto proposed = history.document;
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
            return rejected(history.document.revision, diagnostic_code_t::invalid_resolution,
                            slices.diagnostics.front().message);
        }
    }
    return commit_document_command(history, std::move(proposed), "Delete BMSON BGM marker");
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
        (!refs.note || (refs.note->kind == note_kind_t::tap && !refs.note->end &&
                        !refs.note->label && !refs.note->branch &&
                        refs.note->semantic_layers.empty() && refs.note->sound_channel_event)) &&
        (!refs.bgm || (!refs.bgm->branch && refs.bgm->sound_channel_event));
}

auto target_assignment_lane(const chart_document_t& chart, const measure_index_t& index,
                            const semantic_event_refs_t& refs,
                            std::optional<lane_id_t> lane, std::uint64_t revision)
    -> std::optional<document_command_result_t> {
    if (!lane) return std::nullopt;
    const auto position = refs.note ? refs.note->position : refs.bgm->position;
    const auto row = absolute_row(index, position);
    if (!row)
        return rejected(revision, diagnostic_code_t::row_out_of_range,
                        "BMSON sound assignment target is outside chart geometry");
    if (!editable_lane_at(chart, index, *lane, *row))
        return rejected(revision, diagnostic_code_t::unknown_lane,
                        "Target playable lane is unavailable at the BMSON marker");
    for (const auto& measure : chart.measures) {
        for (const auto& other : measure.notes) {
            if (other.id == (refs.note ? refs.note->id : refs.bgm->id) || other.lane != *lane)
                continue;
            const auto begin = absolute_row(index, other.position);
            const auto end = other.end ? absolute_row(index, *other.end) : begin;
            if (begin && end && *begin <= *row && *row <= *end)
                return rejected(revision, diagnostic_code_t::collision,
                                "Another note occupies the target BMSON playable lane");
        }
    }
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
        return rejected(revision, diagnostic_code_t::invalid_id,
                        "BMSON sound assignment source event is unavailable");
    const auto source = std::ranges::find(channel->events, source_id,
                                           &sound_channel_event_t::id);
    if (source == channel->events.end() || source->position != position || source->end ||
        source->playable != (refs.note != nullptr) ||
        (refs.note && source->lane != refs.note->lane) || (refs.bgm && source->lane))
        return rejected(revision, diagnostic_code_t::invalid_id,
                        "BMSON semantic event and retained source disagree");
    if (std::ranges::any_of(channel->events, [&](const auto& other) {
            return other.id != source_id && other.position == position;
        }))
        return rejected(revision, diagnostic_code_t::collision,
                        "Another event shares this BMSON source pulse");
    return linked_sound_source_t{&*channel, &*source};
}

auto ensure_bgm_source_lane(chart_document_t& chart, std::uint64_t order,
                            std::string_view channel_name, std::uint64_t revision)
    -> std::variant<object_id_t, document_command_result_t> {
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
        return rejected(revision, diagnostic_code_t::invalid_id,
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
        measure.notes.push_back({.id = refs.bgm->id, .position = position, .lane = *lane,
            .kind = note_kind_t::tap, .end = std::nullopt, .sound = refs.bgm->sound,
            .sound_channel_event = source_id, .provenance = refs.bgm->provenance});
        std::erase_if(measure.bgm_events,
                      [semantic_id](const auto& item) { return item.id == semantic_id; });
        return std::nullopt;
    }
    auto lane_result = ensure_bgm_source_lane(chart, channel_order, channel_name, revision);
    if (const auto* failure = std::get_if<document_command_result_t>(&lane_result)) return *failure;
    const auto source_lane = std::get<object_id_t>(lane_result);
    measure.bgm_events.push_back({.id = refs.note->id, .position = position,
        .source_lane = source_lane, .sound = refs.note->sound,
        .sound_channel_event = source_id, .structural_slot = 0,
        .provenance = refs.note->provenance});
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
        return rejected(revision, diagnostic_code_t::invalid_id,
                        "BMSON sound assignment has a stale document revision");
    const auto found_chart =
        std::ranges::find(history.document.charts, chart, &chart_document_t::id);
    if (!chart || found_chart == history.document.charts.end() ||
        found_chart->source_family != format_family_t::bmson || !semantic_event ||
        (playable_lane && !*playable_lane))
        return rejected(revision, diagnostic_code_t::invalid_id,
                        "BMSON sound assignment requires a linked event and valid target");
    const auto built = build_measure_index(*found_chart);
    if (!std::holds_alternative<measure_index_t>(built))
        return rejected(revision, diagnostic_code_t::invalid_measure,
                        "BMSON sound assignment requires valid measure geometry");
    const auto& index = std::get<measure_index_t>(built);
    const auto refs = find_semantic_event(*found_chart, semantic_event);
    if (!valid_semantic_event(refs))
        return rejected(revision, diagnostic_code_t::invalid_id,
                        "BMSON sound assignment requires one linked tap or BGM marker");
    if ((refs.note && playable_lane && refs.note->lane == *playable_lane) ||
        (refs.bgm && !playable_lane))
        return {.applied = false, .revision = revision, .diagnostics = {}};
    if (auto failure = target_assignment_lane(*found_chart, index, refs, playable_lane, revision))
        return *failure;
    const auto position = refs.note ? refs.note->position : refs.bgm->position;
    const auto target_row = absolute_row(index, position);
    if (!target_row)
        return rejected(revision, diagnostic_code_t::row_out_of_range,
                        "BMSON sound assignment target is outside chart geometry");
    const auto source_id = refs.note ? *refs.note->sound_channel_event
                                     : *refs.bgm->sound_channel_event;
    const auto source_result = find_linked_sound_source(*found_chart, source_id, position,
                                                        refs, revision);
    if (const auto* failure = std::get_if<document_command_result_t>(&source_result))
        return *failure;
    const auto source = std::get<linked_sound_source_t>(source_result);
    auto proposed = history.document;
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
        return rejected(revision, diagnostic_code_t::invalid_measure,
                        "BMSON sound assignment measure is unavailable");
    auto& changed_measure = *destination;
    if (auto failure = update_semantic_assignment(
            changed_chart, changed_measure, refs, semantic_event, source_id, playable_lane,
            channel_offset, source.channel->name, revision)) return *failure;
    if (auto failure = validate_channel_slices(
            proposed, changed_chart, changed_chart.sound_channels[channel_offset], revision))
        return *failure;
    return commit_document_command(history, std::move(proposed),
                                   playable_lane ? "Assign BMSON sound to playable lane"
                                                 : "Assign BMSON sound to BGM");
}

} // namespace wormedit
