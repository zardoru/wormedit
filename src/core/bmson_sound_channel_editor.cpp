#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/measure.h>
#include <wormedit/core/sound_slice.h>

#include <algorithm>
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

} // namespace wormedit
