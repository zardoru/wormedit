#include "chart_object_ids.h"
#include <wormedit/core/bmson_lane_mode.h>
#include <wormedit/core/bmson_sound_channel_editor.h>

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <unordered_map>
#include <set>
#include <map>

namespace wormedit {
namespace {

using event_ids_t = std::unordered_set<std::uint64_t>;
auto channel_events(const sound_channel_t& channel) -> event_ids_t {
    event_ids_t ids;
    for (const auto& event : channel.events) ids.insert(event.id.value);
    return ids;
}
auto is_channel_event(const bgm_event_t& event, const event_ids_t& ids) -> bool {
    return event.sound_channel_event && ids.contains(event.sound_channel_event->value);
}

auto make_stem(chart_document_t& chart, const sound_channel_t& channel,
               const event_ids_t& ids) -> std::string {
    auto lane = std::ranges::find(chart.bgm_source_lanes, std::optional{channel.id},
                                  &bgm_source_lane_t::stem_channel);
    if (lane == chart.bgm_source_lanes.end()) {
        const auto largest = object_id_ceiling_t::from(chart).maximum;
        std::size_t order{};
        for (const auto& item : chart.bgm_source_lanes) {
            if (item.source_order == std::numeric_limits<std::size_t>::max())
                return "BGM source order space is exhausted";
            order = std::max(order, item.source_order + 1);
        }
        if (largest == std::numeric_limits<std::uint64_t>::max() ||
            chart.bgm_source_lanes.size() >= default_maximum_chart_objects)
            return "BGM lane ID or capacity is exhausted";
        chart.bgm_source_lanes.push_back({.id = object_id_t{largest + 1},
            .name = channel.name, .source_order = order, .stem_channel = channel.id});
        lane = std::prev(chart.bgm_source_lanes.end());
    }
    for (auto& measure : chart.measures)
        for (auto& event : measure.bgm_events)
            if (is_channel_event(event, ids)) event.source_lane = lane->id;
    return {};
}

// Feed display markers to the established T-423 planner as temporary ordinary
// notes. The proposed source graph and semantic objects never take this shape.
auto pack_channel(chart_document_t& chart, const sound_channel_t& channel, const event_ids_t& ids,
                  std::span<const resource_entry_t> resources, note_to_bgm_policy_t policy)
    -> note_to_bgm_allocation_outcome_t {
    const auto packed_count = std::ranges::count_if(
        chart.bgm_source_lanes, [](const auto& lane) { return !lane.stem_channel; });
    if (static_cast<std::size_t>(packed_count) > policy.max_lanes)
        return {.diagnostics = {{diagnostic_severity_t::error,
                                 diagnostic_code_t::invalid_id,
                                 "existing packed BGM columns exceed the configured maximum",
                                 {}}}};
    auto allocation = chart;
    std::vector<object_id_t> selected;
    std::erase_if(allocation.bgm_source_lanes,
                  [&](const auto& lane) { return lane.stem_channel == channel.id; });
    for (auto& measure : allocation.measures) {
        for (const auto& event : measure.bgm_events) {
            if (!is_channel_event(event, ids))
                continue;
            selected.push_back(event.id);
            measure.notes.push_back({.id = event.id,
                                     .position = event.position,
                                     .sound = event.sound,
                                     .branch = event.branch});
        }
        std::erase_if(measure.bgm_events,
                      [&](const auto& event) { return is_channel_event(event, ids); });
    }
    if (selected.empty())
        return {};
    return note_to_bgm_allocation_t::from(allocation, resources, selected, std::nullopt, policy);
}

auto apply_packing(chart_document_t& chart, const sound_channel_t& channel,
                   const note_to_bgm_allocation_t& plan) -> void {
    std::erase_if(chart.bgm_source_lanes,
                  [&](const auto& lane) { return lane.stem_channel == channel.id; });
    for (const auto& lane : plan.new_lanes)
        chart.bgm_source_lanes.push_back(
            {.id = lane.id, .name = lane.name, .source_order = lane.source_order});
    std::unordered_map<std::uint64_t, object_id_t> assignments;
    for (const auto& assignment : plan.assignments)
        assignments.emplace(assignment.note.value, assignment.lane);
    for (auto& measure : chart.measures)
        for (auto& event : measure.bgm_events)
            if (const auto found = assignments.find(event.id.value); found != assignments.end())
                event.source_lane = found->second;
}

auto semantic_resource_channels(const chart_document_t& chart)
    -> std::unordered_map<std::uint64_t, std::set<object_id_t>> {
    std::unordered_map<std::uint64_t, object_id_t> events;
    for (const auto& channel : chart.sound_channels)
        for (const auto& event : channel.events) events.emplace(event.id.value, channel.id);
    std::unordered_map<std::uint64_t, std::set<object_id_t>> resources;
    const auto record = [&](const auto& event) {
        if (!event.sound || !event.sound_channel_event) return;
        const auto found = events.find(event.sound_channel_event->value);
        if (found != events.end()) resources[event.sound->value].insert(found->second);
    };
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) record(note);
        for (const auto& event : measure.bgm_events) record(event);
    }
    return resources;
}
auto channel_resources(const song_document_t& document, chart_id_t chart, object_id_t channel)
    -> std::vector<resource_id_t> {
    std::vector<resource_id_t> resources;
    const auto owner = document.find_chart(chart);
    if (owner == nullptr) return {};
    const auto memberships = semantic_resource_channels(*owner);
    for (const auto& binding : bmson_lane_mode_resource_bindings(document, chart)) {
        const auto member = memberships.find(binding.resource.value);
        if (binding.ambiguous && member != memberships.end() && member->second.contains(channel)) return {};
        if (!binding.ambiguous && binding.channel == channel) resources.push_back(binding.resource);
    }
    return resources;
}
using empty_key_t = std::pair<std::uint64_t, std::string>;
struct empty_bindings_t {
    std::map<empty_key_t, std::vector<object_id_t>> channels;
    std::map<empty_key_t, std::size_t> resources;
};
auto index_empty_channels(const song_document_t& document, const chart_document_t& chart)
    -> empty_bindings_t {
    empty_bindings_t result;
    for (const auto& channel : chart.sound_channels)
        if (channel.events.empty())
            result.channels[{channel.source_clip.source.value, channel.name}].push_back(channel.id);
    for (const auto& resource : document.resources)
        ++result.resources[{resource.asset.value, resource.name}];
    return result;
}
auto empty_channel_binding(const empty_bindings_t& index, const resource_entry_t& resource)
    -> bmson_channel_resource_binding_t {
    bmson_channel_resource_binding_t result{.resource = resource.id};
    const empty_key_t key{resource.asset.value, resource.name};
    const auto channels = index.channels.find(key);
    if (channels == index.channels.end()) return result;
    const auto resources = index.resources.find(key);
    result.ambiguous = channels->second.size() != 1 || resources == index.resources.end() || resources->second != 1;
    if (!result.ambiguous) result.channel = channels->second.front();
    return result;
}
auto is_lane_mode_current(const song_document_t& document, const chart_document_t& chart,
                     const sound_channel_t& channel, std::span<const resource_id_t> resources,
                     bmson_channel_lane_mode_t mode) -> bool {
    const auto kind = mode == bmson_channel_lane_mode_t::stem ? sound_kind_t::stem : sound_kind_t::keysound;
    const auto lane = std::ranges::find(chart.bgm_source_lanes, std::optional{channel.id},
                                      &bgm_source_lane_t::stem_channel);
    if ((lane != chart.bgm_source_lanes.end()) != (mode == bmson_channel_lane_mode_t::stem)) return false;
    for (const auto& resource : document.resources)
        if (std::ranges::find(resources, resource.id) != resources.end() && resource.sound_kind != kind)
            return false;
    if (mode == bmson_channel_lane_mode_t::stem) {
        const auto ids = channel_events(channel);
        for (const auto& measure : chart.measures)
            for (const auto& event : measure.bgm_events)
                if (is_channel_event(event, ids) && event.source_lane != lane->id) return false;
    }
    return true;
}
} // namespace

auto bmson_lane_mode_resource_bindings(const song_document_t& document, chart_id_t id)
    -> std::vector<bmson_channel_resource_binding_t> {
    const auto chart = document.find_chart(id);
    if (chart == nullptr || chart->source_family != format_family_t::bmson) return {};
    const auto channels = semantic_resource_channels(*chart);
    const auto empty = index_empty_channels(document, *chart);
    std::vector<bmson_channel_resource_binding_t> result;
    for (const auto& resource : document.resources) {
        const auto found = channels.find(resource.id.value);
        if (found == channels.end()) {
            result.push_back(empty_channel_binding(empty, resource));
            continue;
        }
        const bool ambiguous = found->second.size() != 1;
        result.push_back({resource.id, ambiguous ? std::nullopt : std::optional{*found->second.begin()}, ambiguous});
    }
    return result;
}

auto set_bmson_channel_lane_mode(document_history_t& history,
    chart_id_t chart_id, object_id_t channel_id, bmson_channel_lane_mode_t mode,
    bool convert_continuations_to_restarts, note_to_bgm_policy_t policy)
    -> document_command_result_t {
    const auto revision = history.document.revision;
    auto transaction = document_command_transaction_t(history);
    auto& proposed = transaction.document();
    const auto chart = proposed.find_chart(chart_id);
    if (chart == nullptr || chart->source_family != format_family_t::bmson)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id, "Lane mode requires a known BMSON chart");
    const auto channel = std::ranges::find(chart->sound_channels, channel_id, &sound_channel_t::id);
    if (channel == chart->sound_channels.end()) return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id, "Unknown BMSON sound channel");
    if (mode != bmson_channel_lane_mode_t::stem && mode != bmson_channel_lane_mode_t::packed)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id, "Unknown BMSON lane mode");
    const auto resources = channel_resources(proposed, chart_id, channel_id);
    if (resources.empty()) return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id, "BMSON channel has no unambiguous sound resource");
    const bool continues = std::ranges::any_of(channel->events,
        [](const auto& event) { return event.continuation; });
    if (mode == bmson_channel_lane_mode_t::packed && continues && !convert_continuations_to_restarts)
        return document_command_transaction_t::rejection(revision, diagnostic_code_t::invalid_id, "continuation points need a stem lane");
    if (!convert_continuations_to_restarts && is_lane_mode_current(proposed, *chart, *channel, resources, mode))
        return {.revision = revision};
    const auto ids = channel_events(*channel);
    if (mode == bmson_channel_lane_mode_t::stem) {
        const auto error = make_stem(*chart, *channel, ids);
        if (!error.empty())
            return document_command_transaction_t::rejection(revision,
                                                             diagnostic_code_t::invalid_id, error);
    } else {
        const auto plan = pack_channel(*chart, *channel, ids, proposed.resources, policy);
        if (!plan.diagnostics.empty())
            return {.revision = revision, .diagnostics = plan.diagnostics};
        apply_packing(*chart, *channel, plan.value);
        if (convert_continuations_to_restarts)
            for (auto& event : channel->events)
                event.continuation = false;
    }
    const auto kind = mode == bmson_channel_lane_mode_t::stem ? sound_kind_t::stem : sound_kind_t::keysound;
    for (auto& resource : proposed.resources)
        if (std::ranges::find(resources, resource.id) != resources.end()) resource.sound_kind = kind;
    return transaction.commit(mode == bmson_channel_lane_mode_t::stem ? "Make BMSON stem" : "Pack BMSON channel", {.preserve_retained_source_flags = !convert_continuations_to_restarts});
}
} // namespace wormedit
