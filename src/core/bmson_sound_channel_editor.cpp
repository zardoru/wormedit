#include <wormedit/core/bmson_sound_channel_editor.h>

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

} // namespace wormedit
