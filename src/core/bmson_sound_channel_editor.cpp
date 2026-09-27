#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/measure.h>
#include <wormedit/core/sound_slice.h>

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>

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
    const auto row = target->position;
    const auto has_restart = std::ranges::any_of(channel_index->events, [&](const auto& event) {
        return event.position == row && !event.continuation;
    });
    auto proposed = history.document;
    const auto chart_offset = static_cast<std::size_t>(found_chart - history.document.charts.begin());
    const auto channel_offset = static_cast<std::size_t>(
        channel_index - found_chart->sound_channels.begin());
    auto& events = proposed.charts[chart_offset].sound_channels[channel_offset].events;
    for (auto& event : events) {
        if (event.position == row) {
            event.continuation = has_restart;
        }
    }
    return commit_document_command(history, std::move(proposed),
                                   "Toggle BMSON sound-channel restart");
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
    const std::array markers{bmson_bgm_marker_insertion_t{target, restart}};
    return insert_bmson_bgm_markers(history, expected_revision, chart, reference_bgm_event,
                                     markers);
}

auto insert_bmson_bgm_markers(
    document_history_t& history, std::uint64_t expected_revision, chart_id_t chart,
    object_id_t reference_bgm_event, std::span<const bmson_bgm_marker_insertion_t> markers)
    -> document_command_result_t {
    if (expected_revision != history.document.revision) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON marker insertion has a stale document revision");
    }
    if (markers.empty() || markers.size() > 256) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON marker insertion requires 1 to 256 markers");
    }
    const auto found_chart = std::ranges::find(history.document.charts, chart,
                                                &chart_document_t::id);
    if (!chart || found_chart == history.document.charts.end() ||
        found_chart->source_family != format_family_t::bmson || !reference_bgm_event) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON marker insertion requires a known chart and BGM event");
    }
    const auto measures = build_measure_index(*found_chart);
    if (!std::holds_alternative<measure_index_t>(measures)) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_measure,
                        "BMSON chart has invalid measure geometry");
    }
    for (std::size_t index = 0; index < markers.size(); ++index) {
        if (!absolute_row(std::get<measure_index_t>(measures), markers[index].position)) {
            return rejected(history.document.revision, diagnostic_code_t::row_out_of_range,
                            "BMSON marker target is outside exact chart geometry");
        }
        if (std::ranges::any_of(markers.first(index), [&](const auto& prior) {
                return prior.position == markers[index].position;
            })) {
            return rejected(history.document.revision, diagnostic_code_t::collision,
                            "BMSON marker batch contains the same channel pulse twice");
        }
    }
    const bgm_event_t* reference{};
    for (const auto& measure : found_chart->measures) {
        const auto item = std::ranges::find(measure.bgm_events, reference_bgm_event,
                                            &bgm_event_t::id);
        if (item != measure.bgm_events.end()) {
            reference = &*item;
            break;
        }
    }
    if (!reference || !reference->sound_channel_event || !reference->sound) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "Reference BGM marker has no retained BMSON sound event");
    }
    const auto channel = std::ranges::find_if(found_chart->sound_channels,
        [&](const auto& candidate) {
            return std::ranges::any_of(candidate.events, [&](const auto& event) {
                return event.id == *reference->sound_channel_event;
            });
        });
    if (channel == found_chart->sound_channels.end()) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "Reference BGM source event is unavailable");
    }
    const auto source = std::ranges::find(channel->events, *reference->sound_channel_event,
                                           &sound_channel_event_t::id);
    if (source == channel->events.end() || source->playable ||
        source->position != reference->position) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "Reference BGM marker and retained source event disagree");
    }
    for (const auto& marker : markers) {
        if (std::ranges::any_of(channel->events, [&](const auto& event) {
                return event.position == marker.position;
            })) {
            return rejected(history.document.revision, diagnostic_code_t::collision,
                            "Another event in this BMSON sound channel occupies the target row");
        }
    }

    std::uint64_t largest{};
    std::size_t count{};
    const auto scan = [&](const auto& values) {
        for (const auto& value : values) {
            largest = std::max(largest, value.id.value);
            ++count;
        }
    };
    scan(found_chart->bgm_source_lanes);
    scan(found_chart->lane_layouts);
    scan(found_chart->bpms);
    scan(found_chart->pauses);
    scan(found_chart->warps);
    scan(found_chart->scrolls);
    scan(found_chart->speeds);
    scan(found_chart->kiai);
    scan(found_chart->timing_anchors);
    scan(found_chart->bookmarks);
    scan(found_chart->sections);
    scan(found_chart->bga_events);
    scan(found_chart->sound_channels);
    for (const auto& item : found_chart->sound_channels) {
        scan(item.events);
    }
    for (const auto& measure : found_chart->measures) {
        scan(measure.notes);
        scan(measure.bgm_events);
    }
    const auto needed = markers.size() * 2;
    if (count > 1'000'000 - needed ||
        largest > std::numeric_limits<std::uint64_t>::max() - needed) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON chart object ID space is exhausted");
    }

    auto proposed = history.document;
    const auto chart_offset = static_cast<std::size_t>(found_chart - history.document.charts.begin());
    auto& changed_chart = proposed.charts[chart_offset];
    const auto channel_offset = static_cast<std::size_t>(channel - found_chart->sound_channels.begin());
    auto& changed_channel = changed_chart.sound_channels[channel_offset];
    for (const auto& marker : markers) {
        const object_id_t source_id{++largest};
        const object_id_t bgm_id{++largest};
        changed_channel.events.push_back({.id = source_id, .position = marker.position,
                                          .end = std::nullopt, .lane = {},
                                          .continuation = !marker.restart, .playable = false,
                                          .provenance = {}});
        const auto destination = std::ranges::find(changed_chart.measures,
            marker.position.measure, &measure_chunk_t::id);
        if (destination == changed_chart.measures.end()) {
            return rejected(history.document.revision, diagnostic_code_t::invalid_measure,
                            "BMSON marker destination measure is unavailable");
        }
        destination->bgm_events.push_back({.id = bgm_id, .position = marker.position,
                                           .source_lane = reference->source_lane,
                                           .sound = reference->sound,
                                           .sound_channel_event = source_id,
                                           .structural_slot = 0, .branch = std::nullopt,
                                           .provenance = {}});
    }
    const auto asset = std::ranges::find(proposed.audio_assets,
        changed_channel.source_clip.source, &audio_asset_t::id);
    if (asset != proposed.audio_assets.end() && asset->stream.sample_rate != 0) {
        const auto slices = derive_sound_channel_slices(changed_chart, changed_channel,
            asset->stream.sample_rate, asset->stream.frame_count);
        if (!slices) {
            return rejected(history.document.revision, diagnostic_code_t::invalid_resolution,
                            slices.diagnostics.front().message);
        }
    }
    return commit_document_command(history, std::move(proposed),
                                   markers.size() == 1 ? "Insert BMSON BGM marker"
                                                       : "Insert BMSON BGM markers");
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

auto assign_bmson_sound_event(document_history_t& history, std::uint64_t expected_revision,
                              chart_id_t chart, object_id_t semantic_event,
                              std::optional<lane_id_t> playable_lane) -> document_command_result_t {
    if (expected_revision != history.document.revision) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON sound assignment has a stale document revision");
    }
    const auto found_chart =
        std::ranges::find(history.document.charts, chart, &chart_document_t::id);
    if (!chart || found_chart == history.document.charts.end() ||
        found_chart->source_family != format_family_t::bmson || !semantic_event ||
        (playable_lane && !*playable_lane)) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON sound assignment requires a linked event and valid target");
    }
    const auto built = build_measure_index(*found_chart);
    if (!std::holds_alternative<measure_index_t>(built)) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_measure,
                        "BMSON sound assignment requires valid measure geometry");
    }
    const auto& index = std::get<measure_index_t>(built);
    const note_t* note{};
    const bgm_event_t* bgm{};
    for (const auto& measure : found_chart->measures) {
        if (const auto found = std::ranges::find(measure.notes, semantic_event, &note_t::id);
            found != measure.notes.end()) {
            note = &*found;
        }
        if (const auto found =
                std::ranges::find(measure.bgm_events, semantic_event, &bgm_event_t::id);
            found != measure.bgm_events.end()) {
            bgm = &*found;
        }
    }
    if ((note == nullptr) == (bgm == nullptr) ||
        (note && (note->kind != note_kind_t::tap || note->end || note->label || note->branch ||
                  !note->semantic_layers.empty())) ||
        (bgm && bgm->branch) || (note && !note->sound_channel_event) ||
        (bgm && !bgm->sound_channel_event)) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON sound assignment requires one linked tap or BGM marker");
    }
    if ((note && playable_lane && note->lane == *playable_lane) || (bgm && !playable_lane)) {
        return {.applied = false, .revision = history.document.revision, .diagnostics = {}};
    }
    const auto position = note ? note->position : bgm->position;
    const auto target_row = absolute_row(index, position);
    if (!target_row) {
        return rejected(history.document.revision, diagnostic_code_t::row_out_of_range,
                        "BMSON sound assignment target is outside chart geometry");
    }
    if (playable_lane) {
        const lane_layout_t* layout{};
        std::uint64_t layout_row{};
        for (const auto& segment : found_chart->lane_layouts) {
            const auto row = absolute_row(index, segment.position);
            if (row && *row <= *target_row && (!layout || *row >= layout_row)) {
                layout = &segment.layout;
                layout_row = *row;
            }
        }
        const auto editable =
            layout && std::ranges::any_of(layout->lanes, [&](const auto& lane) {
                return lane.id == *playable_lane && lane.role != lane_role_t::autoplay &&
                       lane.role != lane_role_t::invisible && lane.role != lane_role_t::mine;
            });
        if (!editable) {
            return rejected(history.document.revision, diagnostic_code_t::unknown_lane,
                            "Target playable lane is unavailable at the BMSON marker");
        }
        for (const auto& measure : found_chart->measures) {
            for (const auto& other : measure.notes) {
                if (other.id == semantic_event || other.lane != *playable_lane) {
                    continue;
                }
                const auto begin = absolute_row(index, other.position);
                const auto end = other.end ? absolute_row(index, *other.end) : begin;
                if (begin && end && *begin <= *target_row && *target_row <= *end) {
                    return rejected(history.document.revision, diagnostic_code_t::collision,
                                    "Another note occupies the target BMSON playable lane");
                }
            }
        }
    }
    const auto source_id = note ? *note->sound_channel_event : *bgm->sound_channel_event;
    const auto channel =
        std::ranges::find_if(found_chart->sound_channels, [&](const auto& candidate) {
            return std::ranges::any_of(candidate.events,
                                       [&](const auto& event) { return event.id == source_id; });
        });
    if (channel == found_chart->sound_channels.end()) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON sound assignment source event is unavailable");
    }
    const auto source = std::ranges::find(channel->events, source_id, &sound_channel_event_t::id);
    if (source == channel->events.end() || source->position != position || source->end ||
        source->playable != (note != nullptr) || (note && source->lane != note->lane) ||
        (bgm && source->lane)) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                        "BMSON semantic event and retained source disagree");
    }
    if (std::ranges::any_of(channel->events, [&](const auto& other) {
            return other.id != source_id && other.position == position;
        })) {
        return rejected(history.document.revision, diagnostic_code_t::collision,
                        "Another event shares this BMSON source pulse");
    }
    auto proposed = history.document;
    const auto chart_offset =
        static_cast<std::size_t>(found_chart - history.document.charts.begin());
    auto& changed_chart = proposed.charts[chart_offset];
    const auto channel_offset =
        static_cast<std::size_t>(channel - found_chart->sound_channels.begin());
    const auto source_offset = static_cast<std::size_t>(source - channel->events.begin());
    auto& changed_source = changed_chart.sound_channels[channel_offset].events[source_offset];
    changed_source.playable = playable_lane.has_value();
    changed_source.lane = playable_lane.value_or(lane_id_t{});
    const auto destination =
        std::ranges::find(changed_chart.measures, position.measure, &measure_chunk_t::id);
    if (destination == changed_chart.measures.end()) {
        return rejected(history.document.revision, diagnostic_code_t::invalid_measure,
                        "BMSON sound assignment measure is unavailable");
    }
    auto& changed_measure = *destination;
    if (playable_lane) {
        changed_measure.notes.push_back({.id = bgm->id,
                                         .position = position,
                                         .lane = *playable_lane,
                                         .kind = note_kind_t::tap,
                                         .end = std::nullopt,
                                         .sound = bgm->sound,
                                         .sound_channel_event = source_id,
                                         .provenance = bgm->provenance});
        std::erase_if(changed_measure.bgm_events,
                      [semantic_event](const auto& item) { return item.id == semantic_event; });
    } else {
        const auto source_order = channel_offset;
        const auto lane =
            std::ranges::find_if(changed_chart.bgm_source_lanes, [&](const auto& candidate) {
                return candidate.source_order == source_order;
            });
        object_id_t bgm_lane{};
        if (lane != changed_chart.bgm_source_lanes.end()) {
            bgm_lane = lane->id;
        } else {
            std::uint64_t largest{};
            const auto scan = [&](const auto& items) {
                for (const auto& item : items)
                    largest = std::max(largest, item.id.value);
            };
            scan(changed_chart.bgm_source_lanes);
            scan(changed_chart.lane_layouts);
            scan(changed_chart.bpms);
            scan(changed_chart.pauses);
            scan(changed_chart.warps);
            scan(changed_chart.scrolls);
            scan(changed_chart.speeds);
            scan(changed_chart.kiai);
            scan(changed_chart.timing_anchors);
            scan(changed_chart.bookmarks);
            scan(changed_chart.sections);
            scan(changed_chart.bga_events);
            scan(changed_chart.sound_channels);
            for (const auto& item : changed_chart.sound_channels)
                scan(item.events);
            for (const auto& measure : changed_chart.measures) {
                scan(measure.notes);
                scan(measure.bgm_events);
            }
            if (largest == std::numeric_limits<std::uint64_t>::max()) {
                return rejected(history.document.revision, diagnostic_code_t::invalid_id,
                                "BMSON BGM lane ID space is exhausted");
            }
            bgm_lane = object_id_t{largest + 1};
            const auto insert_at = std::ranges::lower_bound(
                changed_chart.bgm_source_lanes, source_order, {},
                &bgm_source_lane_t::source_order);
            changed_chart.bgm_source_lanes.insert(
                insert_at, {.id = bgm_lane, .name = channel->name,
                            .source_order = source_order});
        }
        changed_measure.bgm_events.push_back({.id = note->id,
                                              .position = position,
                                              .source_lane = bgm_lane,
                                              .sound = note->sound,
                                              .sound_channel_event = source_id,
                                              .structural_slot = 0,
                                              .provenance = note->provenance});
        std::erase_if(changed_measure.notes,
                      [semantic_event](const auto& item) { return item.id == semantic_event; });
    }
    const auto asset = std::ranges::find(
        proposed.audio_assets, changed_chart.sound_channels[channel_offset].source_clip.source,
        &audio_asset_t::id);
    if (asset != proposed.audio_assets.end() && asset->stream.sample_rate != 0) {
        const auto slices =
            derive_sound_channel_slices(changed_chart, changed_chart.sound_channels[channel_offset],
                                        asset->stream.sample_rate, asset->stream.frame_count);
        if (!slices) {
            return rejected(history.document.revision, diagnostic_code_t::invalid_resolution,
                            slices.diagnostics.front().message);
        }
    }
    return commit_document_command(history, std::move(proposed),
                                   playable_lane ? "Assign BMSON sound to playable lane"
                                                 : "Assign BMSON sound to BGM");
}

} // namespace wormedit
